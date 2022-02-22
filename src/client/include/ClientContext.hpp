#ifndef OPENCMW_CPP_DATASOUCREPUBLISHER_HPP
#define OPENCMW_CPP_DATASOUCREPUBLISHER_HPP

#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <disruptor/Disruptor.hpp>
#include <majordomo/Broker.hpp>
#include <URI.hpp>

namespace opencmw::client {
using namespace std::chrono_literals;
using opencmw::uri_check::STRICT;
using timeUnit                                  = std::chrono::milliseconds;
constexpr static const timeUnit _defaultTimeout = 1000ms; // default interval to check for maintenance tasks
struct RawMessage {                                       // return object for received data
    std::string                           context;        // use context type?
    std::unique_ptr<opencmw::URI<STRICT>> endpoint;
    std::vector<std::byte>                data; // using vector here allows using non zmq transports... try to move data into here without copying or use zmq messages if that is not possible
    std::chrono::milliseconds             timestamp_received = 0s;
};

struct Request {
    URI<STRICT>                       uri;
    std::function<void(RawMessage &)> callback;
    std::chrono::milliseconds         timestamp_received = 0s;
};

struct Subscription {
    URI<STRICT>                       uri;
    std::function<void(RawMessage &)> callback;
    std::chrono::milliseconds         timestamp_received = 0s;
};

struct Command { // TODO: Q: merge with Request/subscription type? just have a single object which gets added to the ring buffer and is then moved to the respective map? -> less objects, less code
    enum class Type {
        Get,
        Set,
        Subscribe,
        Unsubscribe,
        Undefined
    };
    Type                                    type = Type::Undefined;
    std::unique_ptr<URI<STRICT>>            uri;
    std::function<void(const RawMessage &)> callback; // callback or target ring buffer
    std::vector<std::byte>                  data;     // data for set, can also contain filters etc for other types
    std::chrono::milliseconds               timestamp_received = 0s;
};

static constexpr std::size_t CMD_RB_SIZE = 32;
using CmdBufferType                      = opencmw::disruptor::RingBuffer<Command, CMD_RB_SIZE, opencmw::disruptor::SpinWaitWaitStrategy>;
using CmdPollerType                      = disruptor::EventPoller<Command, CMD_RB_SIZE, disruptor::SpinWaitWaitStrategy, disruptor::MultiThreadedStrategy>;

class ClientBase {
public:
    virtual ~ClientBase()                                 = default;
    virtual bool        read(RawMessage &message)         = 0;
    virtual timeUnit    housekeeping(const timeUnit &now) = 0;
    virtual URI<STRICT> endpoint()                        = 0;
};

class ClientCtxBase {
public:
    virtual ~ClientCtxBase() = default;
    // virtual std::unique_ptr<ClientBase> &getClient(const URI<STRICT> &uri)                            = 0;
    virtual std::vector<std::string> protocols()        = 0; // TODO: make this a static function and make ClientCtx a TypeParameter, which is only instantiated once one of the protocols is used
    virtual void                     stop()             = 0;
    virtual void                     request(Command &) = 0;
};

/*
 * Disruptor Publisher which can poll various clients which can get data from different protocols.
 * The commands can be issued by any thread, because the communication is decoupled using a command disruptor.
 */
class ClientContext {
    std::vector<std::unique_ptr<ClientCtxBase>>                            _contexts;
    std::shared_ptr<CmdBufferType>                                         _commandRingBuffer;
    std::shared_ptr<CmdPollerType>                                         _cmdPoller;
    std::unordered_map<std::string, std::reference_wrapper<ClientCtxBase>> _schemeContexts;
    std::jthread                                                           _poller; // thread polling all the sockets

    void                                                                   poll(const std::stop_token &stoken) {
        fmt::print("entering poll loop\n");
        while (!stoken.stop_requested()) { // switch to event processor instead of busy spinning
            handleCommands();
        }
        fmt::print("leaving poll loop\n");
    }

    ClientCtxBase &getClientCtx(const URI<STRICT> &uri) {
        if (_schemeContexts.contains(uri.scheme().value())) {
            return _schemeContexts.at(uri.scheme().value());
        } else {
            throw std::logic_error("no client implementation for scheme");
        }
    };

    void handleCommands() {
        // perform the next scheduled command
        _cmdPoller->poll([this](Command &cmd, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) -> bool {
            if (cmd.type == Command::Type::Undefined) {
                return false;
            }
            auto &c = getClientCtx(*cmd.uri); // add missing clients. for now i didn't find a way to use the returned any to call the functions but have to use the visitor pattern
            // TODO: check if client is already connected to the server in question and if not connect to it? or perform this in the command step?
            // if (c.endpoint()._authority() != receivedEvent.uri->_authority() || c.endpoint().scheme() != receivedEvent.uri->scheme()) return nextHousekeeping < now;
            c.request(cmd);
            return false;
        });
    }

public:
    explicit ClientContext(std::vector<std::unique_ptr<ClientCtxBase>> &&implementations)
        : _contexts(std::move(implementations)), _commandRingBuffer{ std::make_shared<CmdBufferType>() }, _cmdPoller{ _commandRingBuffer->newPoller() } {
        _commandRingBuffer->addGatingSequences({ _cmdPoller->sequence() });
        _poller = std::jthread([this](const std::stop_token &stoken) { this->poll(stoken); });
        for (auto &ctx : _contexts) {
            for (auto &scheme : ctx->protocols()) {
                _schemeContexts.insert({ scheme, std::ref(*ctx) });
            }
        }
    }

    // user interface: can be called from any thread and will return non-blocking after submitting the job to the job queue disruptor
    void get(const URI<STRICT> &endpoint, std::function<void(const RawMessage &)> &&callback) { queueCommand(endpoint, std::move(callback), Command::Type::Get); }
    void set(const URI<STRICT> &endpoint, std::function<void(const RawMessage &)> &&callback, std::vector<std::byte> &&data) { queueCommand(endpoint, std::move(callback), Command::Type::Set, std::move(data)); }
    void subscribe(const URI<STRICT> &endpoint) { queueCommand(endpoint, Command::Type::Subscribe); }
    void subscribe(const URI<STRICT> &endpoint, std::function<void(const RawMessage &)> &&callback) { queueCommand(endpoint, std::move(callback), Command::Type::Subscribe); }
    void unsubscribe(const URI<STRICT> &endpoint) { queueCommand(endpoint, Command::Type::Unsubscribe); }

    /*
     * Shutdown all contexts // TODO: is this necessary or should this be handled by RAII with the poller going out of scope?
     */
    void stop() {
        for (auto &ctx : _contexts) {
            ctx->stop();
        }
        _poller.request_stop(); // request halt to all workers
        _poller.join();         // wait for all workers to be finished
    }

private:
    void queueCommand(const URI<STRICT> &endpoint, Command::Type cmd) {
        _commandRingBuffer->tryPublishEvent([&endpoint, &cmd](Command &&ev, long /*seq*/) {
            ev.type = cmd;
            ev.uri  = std::make_unique<URI<STRICT>>(endpoint);
        });
    }
    void queueCommand(const URI<STRICT> &endpoint, std::function<void(const RawMessage &)> callback, Command::Type cmd) {
        bool published = _commandRingBuffer->tryPublishEvent([&endpoint, &cmd, cb = std::move(callback)](Command &&ev, long /*seq*/) mutable {
            ev.type     = cmd;
            ev.callback = std::move(cb);
            ev.uri      = std::make_unique<URI<STRICT>>(endpoint);
        });
        if (published) {
            fmt::print("command published\n");
        } else {
            fmt::print("failed to publish command\n");
        }
    }
    void queueCommand(const URI<STRICT> &endpoint, std::function<void(const RawMessage &)> &&callback, Command::Type cmd, std::vector<std::byte> &&data) {
        bool published = _commandRingBuffer->tryPublishEvent([&endpoint, &cmd, cb = std::move(callback), d = std::move(data)](Command &&ev, long /*seq*/) mutable {
            ev.type     = cmd;
            ev.callback = std::move(cb);
            ev.uri      = std::make_unique<URI<STRICT>>(endpoint);
            ev.data     = std::move(d);
        });
        if (published) {
            fmt::print("command published\n");
        } else {
            fmt::print("failed to publish command\n");
        }
    }
};

} // namespace opencmw::client
#endif // OPENCMW_CPP_DATASOUCREPUBLISHER_HPP