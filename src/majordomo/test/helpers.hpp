#ifndef MAJORDOMO_TESTS_HELPERS_H
#define MAJORDOMO_TESTS_HELPERS_H

#include <thread>

template<typename T>
concept Shutdownable = requires(T s) {
    s.run();
    s.shutdown();
};

template<Shutdownable T>
struct RunInThread {
    T          &_toRun;
    std::jthread _thread;

    RunInThread(T &toRun)
        : _toRun(toRun)
        , _thread([this] { _toRun.run(); }) {
    }

    ~RunInThread() {
        _toRun.shutdown();
        _thread.join();
    }
};

#endif
