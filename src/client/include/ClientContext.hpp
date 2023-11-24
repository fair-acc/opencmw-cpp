#ifndef OPENCMW_CPP_DATASOUCREPUBLISHER_HPP
#define OPENCMW_CPP_DATASOUCREPUBLISHER_HPP

#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <Debug.hpp>
#include <disruptor/Disruptor.hpp>
#include <MdpMessage.hpp>
#include <URI.hpp>

#include "ClientCommon.hpp"

namespace opencmw::client {

static constexpr std::size_t CMD_RB_SIZE = 32;
using CmdBufferType                      = opencmw::disruptor::RingBuffer<Command, CMD_RB_SIZE, opencmw::disruptor::BlockingWaitStrategy>;
using CmdPollerType                      = disruptor::EventPoller<Command, CMD_RB_SIZE, disruptor::BlockingWaitStrategy, disruptor::MultiThreadedStrategy>;

class ClientBase {
public:
    virtual ~ClientBase()                             = default;
    virtual std::vector<std::string> protocols()      = 0;
    virtual void                     stop()           = 0;
    virtual void                     request(Command) = 0;
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
    std::atomic_bool                                                    _stop_requested{ false };
    std::thread                                                         _poller; // thread polling all the sockets

public:
    explicit ClientContext(std::vector<std::unique_ptr<ClientBase>> &&implementations)
        : _contexts(std::move(implementations)), _commandRingBuffer{ std::make_shared<CmdBufferType>() }, _cmdPoller{ _commandRingBuffer->newPoller() } {
        _commandRingBuffer->addGatingSequences({ _cmdPoller->sequence() });
        _poller = std::thread([this]() { this->poll(_stop_requested); });
        for (auto &ctx : _contexts) {
            for (auto &scheme : ctx->protocols()) {
                _schemeContexts.insert({ scheme, std::ref(*ctx) });
            }
        }
    }

    ~ClientContext() {
        if (!_stop_requested) {
            stop();
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
        _stop_requested = true;
        _poller.join(); // wait for all workers to be finished
    }

private:
    void poll(const std::atomic_bool &stop_requested) {
        while (!stop_requested) { // switch to event processor instead of busy spinning
            _cmdPoller->poll([this](Command &cmd, std::int64_t /*sequenceID*/, bool /*endOfBatch*/) -> bool {
                if (cmd.command == mdp::Command::Invalid) {
                    return false;
                }
                auto &c = getClientCtx(cmd.topic);
#ifdef EMSCRIPTEN
                // this is necessary for fetches to actually be called, as the new thread will start/init/end and then go into js runtime to fetch
                std::thread ql{ [&c, cmd]() {
#endif
                    c.request(cmd);
#ifdef EMSCRIPTEN
                } };
                ql.join();
#endif
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
            ev.topic    = FWD(endpoint);
            ev.data     = std::move(d);
        });
        if (!published) {
            fmt::print("failed to publish command\n");
        }
    }
};

} // namespace opencmw::client
#endif // OPENCMW_CPP_DATASOUCREPUBLISHER_HPP
