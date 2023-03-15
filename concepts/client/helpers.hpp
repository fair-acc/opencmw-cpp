#include <functional>

#include <RestClient.hpp>

#include <cstdlib>

#ifndef CONCEPTS_CLIENT_HELPERS_H
#define CONCEPTS_CLIENT_HELPERS_H

inline auto no_check = [](const opencmw::mdp::Message &) {
    return true;
};

inline auto result_equals = [](std::string data) {
    return [data](const opencmw::mdp::Message &reply) {
        return reply.data.asString() == data;
    };
};

struct rest_test_state {
    std::vector<std::string> log;
    bool                     all_ok = true;

    void                     error(std::string message) {
        std::cerr << message << std::endl;
        log.emplace_back(std::move(message));
        all_ok = false;
    }

    void info(std::string message) {
        log.emplace_back(std::move(message));
    }
};

template<opencmw::mdp::Command command>
struct rest_test_step {
    opencmw::client::RestClient                       &_client;
    opencmw::client::Command                           _command;
    std::function<bool(const opencmw::mdp::Message &)> _resultChecker;
    std::function<void()>                              _continuation;
    std::size_t                                        _expectedRepliesCount = 1;
    std::size_t                                        _repliesCounter       = 0;

    rest_test_state                                    _state;

    rest_test_step(opencmw::client::RestClient &client, auto endpoint, std::function<bool(const opencmw::mdp::Message &)> resultChecker = no_check, std::size_t expectedRepliesCount = 1)
        requires(command != opencmw::mdp::Command::Set)
        : _client(client), _resultChecker(std::move(resultChecker)), _expectedRepliesCount(expectedRepliesCount) {
        _command.command  = command;
        _command.endpoint = endpoint;
        _command.callback = [this](const opencmw::mdp::Message &reply) {
            fmt::print("Reply R\"({})\"\n", reply.data.asString());
            if (_resultChecker && !_resultChecker(reply)) {
                _state.error(fmt::format("ERROR: bad result {}", reply.data.asString()));
            }

            _repliesCounter++;
            if (_expectedRepliesCount == _repliesCounter) {
                next_step();
            }
        };
    }

    rest_test_step(opencmw::client::RestClient &client, auto endpoint, std::string new_data)
        requires(command == opencmw::mdp::Command::Set)
        : _client(client) {
        _command.command  = command;
        _command.data     = opencmw::IoBuffer(new_data.data(), new_data.size());
        _command.endpoint = endpoint;
        _command.callback = [this](const opencmw::mdp::Message &reply) {
            next_step();
        };
    }

    rest_test_step(const rest_test_step &)            = delete;
    rest_test_step &operator=(const rest_test_step &) = delete;

    // Runs this test step
    void run(rest_test_state &&old_state) {
        _state = std::move(old_state);
        _client.request(std::move(_command));
        if (_expectedRepliesCount == 0 /* || command == opencmw::mdp::Command::Set*/) {
            next_step();
        }
    }

    void next_step() {
        if (_continuation) {
            _continuation();
        }
    }

    template<opencmw::mdp::Command next_command>
    rest_test_step<next_command> &and_then(rest_test_step<next_command> &next) {
        _continuation = [this, &next] {
            next.run(std::move(_state));
        };
        return next;
    }

    void finally(auto end) {
        _continuation = [this, &end] {
            end(std::move(_state));
        };
    }

    template<opencmw::mdp::Command next_command>
    auto &operator>>(rest_test_step<next_command> &next) {
        return and_then(next);
    }
};

struct rest_test_runner {
    std::atomic_bool all_done = false;

    template<typename Step, typename... Steps>
    rest_test_runner(Step &step, Steps &...steps) {
        (step >> ... >> steps).finally([this](rest_test_state &&state) {
            fmt::print("All tests finished.\n");
            if (!state.all_ok) {
                for (const auto &line : state.log) {
                    std::cerr << line << std::endl;
                }
            }
#ifdef __EMSCRIPTEN__
            EM_ASM(document.title = 'DONE';);
#else
            if (!state.all_ok) std::terminate();
#endif
        });

        step.run(rest_test_state{});
    }

    rest_test_runner(const rest_test_runner &)            = delete;
    rest_test_runner &operator=(const rest_test_runner &) = delete;
};

#endif // include guard
