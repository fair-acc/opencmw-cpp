#ifndef MAJORDOMO_TESTS_HELPERS_H
#define MAJORDOMO_TESTS_HELPERS_H

#include <yaz/Meta.hpp>

#include <thread>

template <typename T>
concept Shutdownable = requires(T s) {
    s.run();
    s.shutdown();
};

template <Shutdownable T>
struct RunInThread {
    T& _to_run;
    std::thread _thread;

    RunInThread(T &to_run)
        : _to_run(to_run)
        , _thread([this] { _to_run.run(); }) {
    }

    ~RunInThread() {
        _to_run.shutdown();
        _thread.join();
    }
};

#endif
