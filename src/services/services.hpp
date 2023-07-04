#ifndef OPENCMW_CPP_SERVICES_HPP
#define OPENCMW_CPP_SERVICES_HPP

#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>

#include <thread>

namespace opencmw {
namespace service {

class RunDefaultBroker {
protected:
    majordomo::Broker<>                _broker{ "Broker", {} };
    std::vector<std::jthread>          _threads;
    std::vector<std::function<void()>> _shutdowns;
    std::mutex                         _mutex;
    std::mutex                         _setup;

public:
    majordomo::Broker<> &getBroker() {
        return _broker;
    }

    template<typename WorkerT, typename HandlerT>
    void runWorker() {
        std::lock_guard         m{ _setup };
        std::mutex              foo;
        std::condition_variable cv;
        bool                    ready{ false };

        _threads.push_back(std::move(std::jthread{
                [&]() {
                    WorkerT w{ this->_broker, HandlerT{} };
                    {
                        std::lock_guard guard{ _mutex };
                        this->_shutdowns.push_back([&w]() { w.shutdown(); });
                    }
                    ready = true;
                    cv.notify_all();
                    w.run();
                } }));
        std::unique_lock lock{ foo };
        cv.wait(lock, [&ready] { return ready; });
    }

    ~RunDefaultBroker() {
        std::lock_guard m{ _setup };
        std::lock_guard guard{ _mutex };
        _broker.shutdown();
        std::for_each(_shutdowns.rbegin(), _shutdowns.rend(), [](auto &t) { t(); });

        std::for_each(_threads.rbegin(), _threads.rend(), [](auto &t) { t.join(); });
    }

    RunDefaultBroker() {
        std::lock_guard         m{ _setup };
        std::condition_variable cv;
        std::mutex              _m;
        std::unique_lock        lock{ _m };
        bool                    ready{ false };

        _threads.push_back(std::move(std::jthread{ [this, &cv, &ready] {
            std::string                                                 rootPath{ "./" };
            auto                                                        fs = cmrc::assets::get_filesystem();
            majordomo::RestBackend<majordomo::PLAIN_HTTP, decltype(fs)> backend{ _broker, fs };
            {
                std::lock_guard guard{ _mutex };
                this->_shutdowns.push_back([&backend]() { backend.shutdown(); });
            }
            ready = true;
            cv.notify_all();
            backend.run();
        } }));

        cv.wait(lock, [&ready] { return ready; });
    }

    void startBroker() {
        std::lock_guard         l{ _setup };
        std::lock_guard         ltt{ _mutex };
        std::condition_variable cv;
        std::mutex              _m;
        std::unique_lock        lock{ _m };
        bool                    ready{ false };

        _threads.push_back(std::jthread{
                [this, &cv, &ready] {
                    ready = true;
                    cv.notify_all();
                    _broker.run();
                } });
        cv.wait(lock, [&ready] { return ready; });
    }
};

}
} // namespace opencmw::service

#endif // OPENCMW_CPP_SERVICES_HPP
