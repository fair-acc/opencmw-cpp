#include <atomic>
#include <mutex>
#include <string_view>

#include <MIME.hpp>
#include <RestClient.hpp>
#include <URI.hpp>

namespace detail {
class EventDispatcher {
    std::mutex              _mutex;
    std::condition_variable _condition;
    std::atomic<int>        _id{ 0 };
    std::atomic<int>        _cid{ -1 };
    std::string             _message;

public:
    void wait_event(httplib::DataSink &sink) {
        std::unique_lock lk(_mutex);
        int              id = std::atomic_load_explicit(&_id, std::memory_order_acquire);
        _condition.wait(lk, [id, this] { return _cid == id; });
        if (sink.is_writable()) {
            sink.write(_message.data(), _message.size());
        }
    }

    void send_event(const std::string_view &message) {
        std::scoped_lock lk(_mutex);
        _cid     = _id++;
        _message = message;
        _condition.notify_all();
    }
};
} // namespace detail

int main() {
    using namespace std::chrono_literals;
    opencmw::client::RestClient client;

    std::atomic<int>            updateCounter{ 0 };
    detail::EventDispatcher     eventDispatcher;
    httplib::Server             server;
    server.Get("/event", [&eventDispatcher, &updateCounter](const httplib::Request &req, httplib::Response &res) {
        auto acceptType = req.headers.find("accept");
        if (acceptType == req.headers.end() || opencmw::MIME::EVENT_STREAM.typeName() != acceptType->second) { // non-SSE request -> return default response
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
            res.set_content(fmt::format("update counter = {}", updateCounter), opencmw::MIME::TEXT);
#else
            res.set_content(fmt::format("update counter = {}", updateCounter), std::string(opencmw::MIME::TEXT.typeName()));
#endif
            return;
        } else {
            fmt::print("server received SSE request on path '{}' body = '{}'\n", req.path, req.body);
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
            res.set_chunked_content_provider(opencmw::MIME::EVENT_STREAM, [&eventDispatcher](size_t /*offset*/, httplib::DataSink &sink) {
#else
            res.set_chunked_content_provider(std::string(opencmw::MIME::EVENT_STREAM.typeName()), [&eventDispatcher](size_t /*offset*/, httplib::DataSink &sink) {
#endif
                eventDispatcher.wait_event(sink);
                return true;
            });
        }
    });
    server.Get("/endPoint", [](const httplib::Request &req, httplib::Response &res) {
        fmt::print("server received request on path '{}' body = '{}'\n", req.path, req.body);
        res.set_content("Hello World!", "text/plain");
    });
    client.threadPool()->execute<"RestServer">([&server] { server.listen("localhost", 8080); });
    int timeOut = 0;
    while (!server.is_running() && timeOut < 10'000) {
        std::this_thread::sleep_for(1ms);
        timeOut += 1;
    }
    assert(server.is_running());
    if (!server.is_running()) {
        fmt::print("couldn't start server\n");
        std::terminate();
    }

    std::atomic<int>  received(false);
    opencmw::IoBuffer data;
    data.put('A');
    data.put('B');
    data.put('C');
    data.put(0);
    opencmw::client::Command command;
    command.command  = opencmw::mdp::Command::Subscribe;
    command.endpoint = opencmw::URI<opencmw::STRICT>("http://localhost:8080/event");
    command.data     = std::move(data);
    command.callback = [&received](const opencmw::mdp::Message &rep) {
        fmt::print("SSE client received reply = '{}' - body size: '{}'\n", rep.data.asString(), rep.data.size());
        received.fetch_add(1, std::memory_order_relaxed);
        received.notify_all();
    };

    client.request(command);

    std::cout << "client request launched" << std::endl;
    std::this_thread::sleep_for(100ms);
    eventDispatcher.send_event("test-event meta data");
    std::jthread([&updateCounter, &eventDispatcher] {
        while (updateCounter < 5) {
            std::this_thread::sleep_for(500ms);
            eventDispatcher.send_event(fmt::format("test-event {}", updateCounter++));
        }
    }).join();

    while (received.load(std::memory_order_relaxed) < 5) {
        std::this_thread::sleep_for(100ms);
    }
    std::cout << "done waiting" << std::endl;
    assert(received.load(std::memory_order_acquire) >= 5);

    command.command = opencmw::mdp::Command::Unsubscribe;
    client.request(command);
    std::this_thread::sleep_for(100ms);
    std::cout << "done Unsubscribe" << std::endl;
    client.stop();
    std::cout << "client stopped" << std::endl;

    server.stop();
    eventDispatcher.send_event(fmt::format("test-event {}", updateCounter++));
    std::cout << "server stopped" << std::endl;
    std::this_thread::sleep_for(5s);

    return 0;
}
