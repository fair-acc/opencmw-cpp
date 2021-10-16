#include <iostream>
#include <random>

#include <yaz/Context.hpp>
#include <yaz/Debug.hpp>
#include <yaz/Message.hpp>
#include <yaz/Socket.hpp>
#include <yaz/SocketGroup.hpp>

template<typename Handler>
class base_socket : public yaz::Socket<Handler> {
public:
    explicit base_socket(yaz::Context &context, yaz::SocketType type, Handler &&handler)
        : yaz::Socket<Handler>(context, type, std::move(handler)) {
        // socket.setHWM(HIGH_WATER_MARK);
        // socket.setHeartbeatContext(PROT_CLIENT.getData());
        // socket.setHeartbeatTtl(HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS);
        // socket.setHeartbeatTimeout(HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS);
        // socket.setHeartbeatIvl(HEARTBEAT_INTERVAL);
        // socket.setLinger(HEARTBEAT_INTERVAL);
    }
};

template<typename Handler>
class router_socket : public base_socket<Handler> {
public:
    using base_socket<Handler>::bind;

    explicit router_socket(yaz::Context &context, Handler &&handler)
        : base_socket<Handler>(context, yaz::SocketType::Router, std::move(handler)) {
        debug() << "Creating an instance of router_socket\n";
        bind("inproc://broker/router");
    }
};

template<typename Handler>
class socket_type_1 : public yaz::Socket<Handler> {
public:
    explicit socket_type_1(yaz::Context &context, Handler &&handler)
        : yaz::Socket<Handler>(context, yaz::SocketType::XPublish, std::move(handler)) {
        debug() << "Creating an instance of socket\n";
    }
};

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
class publisher_socket : public yaz::Socket<Handler> {
public:
    using yaz::Socket<Handler>::bind;

    explicit publisher_socket(yaz::Context &context, Handler &&handler)
        : yaz::Socket<Handler>(context, yaz::SocketType::Publish, std::move(handler)) {
        bind(address);
    }
};

template<typename Handler>
class subscriber_socket : public yaz::Socket<Handler> {
public:
    using yaz::Socket<Handler>::connect;

    explicit subscriber_socket(yaz::Context &context, Handler &&handler)
        : yaz::Socket<Handler>(context, yaz::SocketType::Subscribe, std::move(handler)) {
        connect(address);
        _id = s_last_id++;
    }

    int               id() const { return _id; }
    int               _id;
    inline static int s_last_id = 0;
};

struct sub_g_handler {
    bool receive_message(auto &socket, bool /*something*/) {
        socket.read([&](auto &&message) {
            for (const auto &part : message) {
                std::cout << socket.id() << "part: [" << part << "]\n";
            }
        });
        // for (const auto &part : message) {
        //     std::cout << "part: [" << part << "]\n";
        // }
        return true;
    }

    bool continue_after_messages_read(bool anything_received) {
        return anything_received;
    }
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: command pub|sub\n";
        return 1;
    }

    std::string_view mode(argv[1]);

    yaz::Context     context;

    if (mode == "pub") {
        yaz::Socket publisher(
                context, yaz::SocketType::Publish, [](auto && /*socket*/, auto && /*message*/) { std::cout << "publisher got a message\n"; });
        publisher.bind(address);

        yaz::Message message("Hello");
        while (true) {
            publisher.send(message);
        }

    } else if (mode == "sub") {
        yaz::Socket subscriber(context, yaz::SocketType::Subscribe, [](auto && /*socket*/, auto &&message) {
            std::cout << "subscriber got a message\n";
            for (const auto &part : message) {
                std::cout << "part: [" << part << "]\n";
            }
        });
        subscriber.connect(address);

        while (true) {
            subscriber.read();
        }
    }

    {
        auto group = yaz::make_socket_group<router_socket, socket_type_1, socket_type_1>(context, ignore_all{});
    }

    if (mode == "pub_g") {
        auto  group     = yaz::make_socket_group<publisher_socket>(context);
        auto &publisher = group.get<0>();

        // Let's generate some random messages for debugging purposes
        std::random_device                 rd;
        std::uniform_int_distribution<int> dist(0, 42);
        while (true) {
            yaz::Message message("Hello " + std::to_string(dist(rd)));
            publisher.send(message);
        }

    } else if (mode == "sub_g") {
        auto group = yaz::make_socket_group<subscriber_socket, subscriber_socket>(context, sub_g_handler{});
        // auto &subscriber = group.get<0>();

        while (true) {
            // subscriber.read();
            group.read();
        }
    }
}

