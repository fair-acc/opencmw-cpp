#include <iostream>
#include <random>

#include <yaz/yaz.hpp>

class ignore_all {
public:
    template<typename... Args>
    void operator()(Args &&.../*args*/) const {}

    template<typename Socket>
    bool receive_message(Socket &socket, bool val) {
        return false;
    }

    bool continue_after_messages_read(bool anything_received) {
        return anything_received;
    }
};

const std::string_view address = "ipc:///tmp/0mqtest";

template<typename Handler>
class publisher_socket : public yaz::Socket<yaz::Message, Handler> {
public:
    using yaz::Socket<yaz::Message, Handler>::bind;

    explicit publisher_socket(yaz::Context &context, Handler &&handler)
        : yaz::Socket<yaz::Message, Handler>(context, ZMQ_PUB, std::move(handler)) {
        if (!bind(address)) {
            std::cerr << "Cannot bind publisher socket to " << address << std::endl;
            std::terminate();
        }
    }
};

template<typename Handler>
class subscriber_socket : public yaz::Socket<yaz::Message, Handler> {
public:
    using yaz::Socket<yaz::Message, Handler>::connect;

    explicit subscriber_socket(yaz::Context &context, Handler &&handler)
        : yaz::Socket<yaz::Message, Handler>(context, ZMQ_SUB, std::move(handler)) {
        if (!connect(address)) {
            std::cerr << "Cannot connect subcriber socket to address " << address << std::endl;
            std::terminate();
        }
        _id = s_last_id++;
    }

    int id() const { return _id; }
    int _id;

    // Just counting how many sockets we have
    inline static int s_last_id = 0;
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: command pub|sub\n";
        return 1;
    }

    std::string_view mode(argv[1]);

    yaz::Context     context;

    if (mode == "pub") {
        auto publisher = yaz::make_socket<yaz::Message>(
                context, ZMQ_PUB, [](auto && /*socket*/, auto && /*message*/) { std::cout << "publisher got a message\n"; });
        if (!publisher.bind(address)) {
            std::cerr << "Could not bind publisher to address " << address << std::endl;
            std::terminate();
        }

        while (true) {
            yaz::Message message(std::make_unique<std::string>("Hello"));
            publisher.send(std::move(message));
        }

    } else if (mode == "sub") {
        auto subscriber = yaz::make_socket<yaz::Message>(context, ZMQ_SUB, [](auto && /*socket*/, auto && /*message*/) {
            std::cout << "subscriber got a message\n";
            // for (const auto &part : message) {
            //     std::cout << "part: [" << part << "]\n";
            // }
        });
        if (!subscriber.connect(address)) {
            std::cerr << "Could not connect subscriber to address " << address << std::endl;
            std::terminate();
        }

        while (true) {
            subscriber.read();
        }
    }
}
