#ifndef OPENCMW_CPP_DATASOUCREPUBLISHER_HPP
#define OPENCMW_CPP_DATASOUCREPUBLISHER_HPP

#include "Client.hpp"
#include "IoSerialiserYaS.hpp"
#include <any>
#include <chrono>
#include <collection.hpp>
#include <disruptor/Disruptor.hpp>
#include <disruptor/RoundRobinThreadAffinedTaskScheduler.hpp>
#include <functional>
#include <majordomo/Broker.hpp>
#include <memory>
#include <thread>
#include <URI.hpp>
#include <utility>
#include <vector>

namespace opencmw::client {

using namespace std::chrono_literals;

enum class CommandType {
    Get,
    Set,
    Subscribe,
    Unsubscribe,
    Undefined
};

struct RawMessage {                                                     // return object for received data
    std::string                                                context; // use context type?
    std::unique_ptr<opencmw::URI<opencmw::uri_check::RELAXED>> endpoint;
    std::vector<std::byte>                                     data; // using vector here allows using non zmq transports... try to move data into here without copying or use zmq messages if that is not possible
    std::chrono::milliseconds                                  timestamp_received = 0s;
};

struct Request {
    URI<RELAXED>                      uri;
    std::function<void(RawMessage &)> callback;
    std::chrono::milliseconds         timestamp_received = 0s;
};

struct Subscription {
    URI<RELAXED>                      uri;
    std::function<void(RawMessage &)> callback;
    std::chrono::milliseconds         timestamp_received = 0s;
};

struct Command { // TODO: Q: merge with Request/subscription type? just have a single object which gets added to the ring buffer and is then moved to the respective map? -> less objects, less code
    CommandType                             type = CommandType::Undefined;
    std::unique_ptr<URI<RELAXED>>           uri;
    std::function<void(const RawMessage &)> callback; // callback or target ring buffer
    std::vector<std::byte>                  data;     // data for set, can also contain filters etc for other types
    std::chrono::milliseconds               timestamp_received = 0s;
};

class ClientBase {
public:
    virtual ~ClientBase()                                                                           = default;
    virtual bool                      read(RawMessage &message)                                     = 0;
    virtual std::chrono::milliseconds housekeeping(const std::chrono::milliseconds &now)            = 0;
    virtual URI<RELAXED>              endpoint()                                                    = 0;
    virtual void                      get(const URI<RELAXED> &)                                     = 0;
    virtual void                      set(const URI<RELAXED> &, const std::span<const std::byte> &) = 0;
    virtual void                      subscribe(const URI<RELAXED> &)                               = 0;
    virtual void                      unsubscribe(const URI<RELAXED> &)                             = 0;
};

template<class... T>
auto make_vector(T &&...t) {
    return make_vector(std::to_array({ std::forward<T>(t)... }));
}

/*
 * Disruptor Publisher which can poll various clients which can get data from different protocols.
 * The commands can be issued by any thread, because the communication is decoupled using a command disruptor.
 */
class ClientContext {
    constexpr static const std::chrono::milliseconds _defaultTimeout = 1000ms; // default interval to check for maintenance tasks
    static constexpr std::size_t                     CMD_RB_SIZE     = 32;
    using CmdBufferType                                              = opencmw::disruptor::RingBuffer<Command, CMD_RB_SIZE, opencmw::disruptor::SpinWaitWaitStrategy>;
    using CmdPollerType                                              = disruptor::EventPoller<Command, CMD_RB_SIZE, disruptor::SpinWaitWaitStrategy, disruptor::MultiThreadedStrategy>;
    // keeping track of ongoing requests, maps URIs to subscriptions and other requests
    std::unordered_map<std::string, Request>      _requests;
    std::unordered_map<std::string, Subscription> _subscriptions;

    std::vector<std::unique_ptr<ClientBase>>      _clients;
    std::jthread                                  _poller; // thread polling all the sockets
    std::shared_ptr<CmdBufferType>                _commandRingBuffer;
    std::shared_ptr<CmdPollerType>                _cmdPoller;

public:
    explicit ClientContext(std::vector<std::unique_ptr<ClientBase>> &&clients)
        : _clients{ std::move(clients) }, // move construction for move-only types is hard: https://stackoverflow.com/a/42915152
        _commandRingBuffer{ std::make_shared<CmdBufferType>() }
        , _cmdPoller{ _commandRingBuffer->newPoller() } {
        _commandRingBuffer->addGatingSequences({ _cmdPoller->sequence() });
        _poller = std::jthread([this](const std::stop_token &stoken) { this->poll(stoken); });
    }

    // user interface: can be called from any thread and will return non-blocking after submitting the job to the job queue disruptor
    void get(const URI<uri_check::RELAXED> &endpoint, std::function<void(const RawMessage &)> &&callback) { queueCommand(endpoint, std::move(callback), CommandType::Get); }
    void set(const URI<uri_check::RELAXED> &endpoint, std::function<void(const RawMessage &)> &&callback, std::vector<std::byte> &&data) { queueCommand(endpoint, std::move(callback), CommandType::Set, std::move(data)); }
    void subscribe(const URI<uri_check::RELAXED> &endpoint) { queueCommand(endpoint, CommandType::Subscribe); }
    void subscribe(const URI<uri_check::RELAXED> &endpoint, std::function<void(const RawMessage &)> &&callback) { queueCommand(endpoint, std::move(callback), CommandType::Subscribe); }
    void unsubscribe(const URI<uri_check::RELAXED> &endpoint) { queueCommand(endpoint, CommandType::Unsubscribe); }

    /*
     * Shutdown all worker threads // TODO: is this necessary or should this be handled by RAII with the poller going out of scope?
     */
    void stop() {
        _poller.request_stop(); // request halt to all workers
        _poller.join();         // wait for all workers to be finished
    }

private:
    void queueCommand(const URI<uri_check::RELAXED> &endpoint, CommandType cmd) {
        _commandRingBuffer->tryPublishEvent([&endpoint, &cmd](Command &&ev, long /*seq*/) {
            ev.type = cmd;
            ev.uri  = std::make_unique<URI<RELAXED>>(endpoint);
        });
    }
    void queueCommand(const URI<uri_check::RELAXED> &endpoint, std::function<void(const RawMessage &)> callback, CommandType cmd) {
        bool published = _commandRingBuffer->tryPublishEvent([&endpoint, &cmd, cb = std::move(callback)](Command &&ev, long /*seq*/) mutable {
            ev.type     = cmd;
            ev.callback = std::move(cb);
            ev.uri      = std::make_unique<URI<RELAXED>>(endpoint);
        });
        if (published) {
            fmt::print("command published\n");
        } else {
            fmt::print("failed to publish command\n");
        }
    }
    void queueCommand(const URI<uri_check::RELAXED> &endpoint, std::function<void(const RawMessage &)> &&callback, CommandType cmd, std::vector<std::byte> &&data) {
        bool published = _commandRingBuffer->tryPublishEvent([&endpoint, &cmd, cb = std::move(callback), d = std::move(data)](Command &&ev, long /*seq*/) mutable {
            ev.type     = cmd;
            ev.callback = std::move(cb);
            ev.uri      = std::make_unique<URI<RELAXED>>(endpoint);
            ev.data     = std::move(d);
        });
        if (published) {
            fmt::print("command published\n");
        } else {
            fmt::print("failed to publish command\n");
        }
    }

    void handleCommands(const std::chrono::milliseconds nextHousekeeping) {
        // perform the next scheduled command. TODO: should this be greedy (up to the housekeeping)
        _cmdPoller->poll([nextHousekeeping, this](Command &cmd, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) -> bool {
            if (cmd.type == CommandType::Undefined) {
                return false;
            }
            auto &c = getClient(*cmd.uri); // add missing clients. for now i didn't find a way to use the returned any to call the functions but have to use the visitor pattern
            // TODO: check if client is allready connected to the server in question and if not connect to it? or perform this in the command step?
            // if (c.endpoint()._authority() != receivedEvent.uri->_authority() || c.endpoint().scheme() != receivedEvent.uri->scheme()) return nextHousekeeping < now;
            switch (cmd.type) {
            case CommandType::Get:
                c->get(*cmd.uri);
                _requests.insert({ cmd.uri->str, Request{ .uri = *cmd.uri, .callback = std::move(cmd.callback), .timestamp_received = cmd.timestamp_received } });
                break;
            case CommandType::Set:
                c->set(*cmd.uri, std::move(cmd.data)); // todo: convert and add actual data
                _requests.insert({ cmd.uri->str, Request{ .uri = *cmd.uri, .callback = std::move(cmd.callback), .timestamp_received = cmd.timestamp_received } });
                break;
            case CommandType::Subscribe:
                fmt::print("subscribing to: {}\n", *cmd.uri);
                if (cmd.callback) {
                    c->subscribe(*cmd.uri);
                    _subscriptions.insert({ cmd.uri->str, Subscription{ .uri = *cmd.uri, .callback = std::move(cmd.callback), .timestamp_received = cmd.timestamp_received } });
                } else {
                    auto foo = c->endpoint().scheme().value();
                    fmt::print("testoutput: {}\n", foo);
                    c->subscribe(*cmd.uri);
                    _subscriptions.insert({ cmd.uri->str, Subscription{ .uri = *cmd.uri, .callback = [](RawMessage &msg) { fmt::print("default handler: msg size: {}\n", msg.data.size()); } } });
                }
                break;
            case CommandType::Unsubscribe:
                c->unsubscribe(*cmd.uri);
                // just remove subscription here or track the state of the subscription
                // con._subscriptions.erase(std::remove_if(con._subscriptions.begin(), con._subscriptions.end(), [&uri](detail::Subscription &sub) {return sub.uri == uri;}), con._subscriptions.end());
                break;
            case CommandType::Undefined: break;
            }
            return false;
        });
    }
    /*
     * Entry point for the poll loop thread
     */
    void poll(const std::stop_token &stoken) {
        fmt::print("entering poll loop\n");
        auto nextHousekeeping = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
        while (!stoken.stop_requested()) {
            handleCommands(nextHousekeeping);
            if (nextHousekeeping < std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())) {
                nextHousekeeping = housekeeping(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()));
                // expire old subscriptions/requests/connections
            }
            for (auto &client : _clients) {
                RawMessage receivedEvent;
                while (bool more = client->read(receivedEvent)) {
                    if (more) {
                        if (_subscriptions.contains(receivedEvent.endpoint->str)) {
                            _subscriptions.at(receivedEvent.endpoint->str).callback(receivedEvent); // callback
                        }
                        if (_requests.contains(receivedEvent.endpoint->str)) {
                            _requests.at(receivedEvent.endpoint->str).callback(receivedEvent); // callback
                            _requests.erase(receivedEvent.endpoint->str);
                        }
                        // _ringBuffer->publishEvent([&client](Event receivedEvent, long /*seq*/) { client.read(receivedEvent); } ); // TODO: add ring buffer interface for subscriptions or globally?
                    }
                    handleCommands(nextHousekeeping);
                    // perform housekeeping duties if necessary
                    if (nextHousekeeping < std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())) {
                        nextHousekeeping = housekeeping(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()));
                    }
                }
            }
            // manage commands: setup new clients if necessary and establish new subscriptions etc
            // todo: remove unused (= no open subscriptions && last request was some time ago) clients after some unused time
        }
        fmt::print("leaving poll loop\n");
    }

    std::unique_ptr<ClientBase> &getClient(URI<RELAXED> uri) {
        auto result = std::find_if(begin(_clients), end(_clients), [&](const auto &c) {
            bool res = c->endpoint().scheme() == uri.scheme();
            return res;
        });
        if (result == std::end(_clients)) {
            throw std::logic_error("protocol not supported");
        }
        return *result;
    }

    /*
     * perform housekeeping functions
     */
    std::chrono::milliseconds housekeeping(std::chrono::milliseconds now) {
        std::chrono::milliseconds next = now + _defaultTimeout;
        for (auto &client : _clients) {
            next = std::min(next, client->housekeeping(now));
        }
        return next;
    }
};

} // namespace opencmw::client
#endif // OPENCMW_CPP_DATASOUCREPUBLISHER_HPP