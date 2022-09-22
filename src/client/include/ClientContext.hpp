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
#include <MdpMessage.hpp>
#include <URI.hpp>

namespace opencmw::client {

using opencmw::uri_check::STRICT;
using timePoint = std::chrono::time_point<std::chrono::system_clock>;
using namespace std::chrono_literals;

struct Request {
    URI<STRICT>                         uri;
    std::function<void(mdp::Message &)> callback;
    timePoint                           timestamp_received = std::chrono::system_clock::now();
};

struct Subscription {
    URI<STRICT>                         uri;
    std::function<void(mdp::Message &)> callback;
    timePoint                           timestamp_received = std::chrono::system_clock::now();
};

struct Command : public mdp::Message {
    std::function<void(const mdp::Message &)> callback; // callback or target ring buffer
};

static constexpr std::size_t CMD_RB_SIZE = 32;
using CmdBufferType                      = opencmw::disruptor::RingBuffer<Command, CMD_RB_SIZE, opencmw::disruptor::BlockingWaitStrategy>;
using CmdPollerType                      = disruptor::EventPoller<Command, CMD_RB_SIZE, disruptor::BlockingWaitStrategy, disruptor::MultiThreadedStrategy>;

class ClientBase {
public:
    virtual ~ClientBase()                               = default;
    virtual std::vector<std::string> protocols()        = 0;
    virtual void                     stop()             = 0;
    virtual void                     request(Command &) = 0;
};

/*
 * ClientContext manages different ClientCtxBase implementations for different protocols and provides a unified API to
 * perform requests on them. Requests can be issued by any thread, because the communication is decoupled using a command disruptor.
 * The requests take a lambda which is used to return the result by the different implementations. This callback should be non-blocking
 * because it might be called from an event loop.
 */
class ClientContext {
    std::vector<std::unique_ptr<ClientBase>>                            _contexts;
    std::shared_ptr<CmdBufferType>                                      _commandRingBuffer;
    std::shared_ptr<CmdPollerType>                                      _cmdPoller;
    std::unordered_map<std::string, std::reference_wrapper<ClientBase>> _schemeContexts;
    std::jthread                                                        _poller; // thread polling all the sockets

public:
    explicit ClientContext(std::vector<std::unique_ptr<ClientBase>> &&implementations)
        : _contexts(std::move(implementations)), _commandRingBuffer{ std::make_shared<CmdBufferType>() }, _cmdPoller{ _commandRingBuffer->newPoller() } {
        _commandRingBuffer->addGatingSequences({ _cmdPoller->sequence() });
        _poller = std::jthread([this](const std::stop_token &stopToken) { this->poll(stopToken); });
        for (auto &ctx : _contexts) {
            for (auto &scheme : ctx->protocols()) {
                _schemeContexts.insert({ scheme, std::ref(*ctx) });
            }
        }
    }

    // user interface: can be called from any thread and will return non-blocking after submitting the job to the job queue disruptor
    void get(const URI<STRICT> &endpoint, std::function<void(const mdp::Message &)> &&callback) { queueCommand(mdp::Command::Get, endpoint, std::move(callback)); }
    void set(const URI<STRICT> &endpoint, std::function<void(const mdp::Message &)> &&callback, IoBuffer &&data) { queueCommand(mdp::Command::Set, endpoint, std::move(callback), std::move(data)); }
    void subscribe(const URI<STRICT> &endpoint) { queueCommand(mdp::Command::Subscribe, endpoint); }
    void subscribe(const URI<STRICT> &endpoint, std::function<void(const mdp::Message &)> &&callback) { queueCommand(mdp::Command::Subscribe, endpoint, std::move(callback)); }
    void unsubscribe(const URI<STRICT> &endpoint) { queueCommand(mdp::Command::Unsubscribe, endpoint); }

    /*
     * Shutdown all contexts
     */
    void stop() {
        for (auto &ctx : _contexts) {
            ctx->stop();
        }
        _poller.request_stop(); // request halt to all workers
        _poller.join();         // wait for all workers to be finished
    }

private:
    void poll(const std::stop_token &stoken) {
        while (!stoken.stop_requested()) { // switch to event processor instead of busy spinning
            _cmdPoller->poll([this](Command &cmd, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) -> bool {
                if (cmd.command == mdp::Command::Invalid) {
                    return false;
                }
                auto &c = getClientCtx(cmd.endpoint);
                c.request(cmd);
                return false;
            });
        }
    }

    ClientBase &getClientCtx(const URI<STRICT> &uri) {
        if (_schemeContexts.contains(uri.scheme().value())) {
            return _schemeContexts.at(uri.scheme().value());
        } else {
            throw std::logic_error("no client implementation for scheme");
        }
    };

    void queueCommand(mdp::Command cmd, const URI<STRICT> &endpoint, std::function<void(const mdp::Message &)> &&callback = {}, IoBuffer &&data = IoBuffer{}) {
        bool published = _commandRingBuffer->tryPublishEvent([&endpoint, &cmd, cb = std::move(callback), d = std::move(data)](Command &&ev, long /*seq*/) mutable {
            ev.command  = cmd;
            ev.callback = std::move(cb);
            ev.endpoint = FWD(endpoint);
            ev.data     = std::move(d);
        });
        if (!published) {
            fmt::print("failed to publish command\n");
        }
    }
};

} // namespace opencmw::client
#endif // OPENCMW_CPP_DATASOUCREPUBLISHER_HPP