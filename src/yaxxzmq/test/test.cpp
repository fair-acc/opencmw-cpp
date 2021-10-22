#include <iostream>
#include <random>

#include <yaz/yaz.hpp>

template<typename Handler>
class base_socket : public yaz::Socket<Handler> {
public:
    explicit base_socket(yaz::Context &context, int type, Handler &&handler)
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
        : base_socket<Handler>(context, ZMQ_ROUTER, std::move(handler)) {
        debug() << "Creating an instance of router_socket\n";
        if (!bind("inproc://broker/router")) {
            std::cerr << "Cannot bind router socket" << std::endl;
            std::terminate();
        }
    }
};

template<typename Handler>
class socket_type_1 : public yaz::Socket<Handler> {
public:
    explicit socket_type_1(yaz::Context &context, Handler &&handler)
        : yaz::Socket<Handler>(context, ZMQ_XPUB, std::move(handler)) {
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
        : yaz::Socket<Handler>(context, ZMQ_PUB, std::move(handler)) {
        if (!bind(address)) {
            std::cerr << "Cannot bind publisher socket to " << address << std::endl;
            std::terminate();
        }
    }
};

template<typename Handler>
class subscriber_socket : public yaz::Socket<Handler> {
public:
    using yaz::Socket<Handler>::connect;

    explicit subscriber_socket(yaz::Context &context, Handler &&handler)
        : yaz::Socket<Handler>(context, ZMQ_SUB, std::move(handler)) {
        if (!connect(address)) {
            std::cerr << "Cannot connect subcriber socket to address " << address << std::endl;
            std::terminate();
        }
        _id = s_last_id++;
    }

    int               id() const { return _id; }
    int               _id;
    inline static int s_last_id = 0;
};

struct sub_g_handler {
    bool receive_message(auto &socket, bool /*something*/) {
        socket.read([&](auto && /*message*/) {
            std::cout << "subscriber got a message\n";
            // for (const auto &part : message) {
            //     std::cout << socket.id() << "part: [" << part << "]\n";
            // }
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
                context, ZMQ_PUB, [](auto && /*socket*/, auto && /*message*/) { std::cout << "publisher got a message\n"; });
        if (!publisher.bind(address)) {
            std::cerr << "Could not bind publisher to address " << address << std::endl;
            std::terminate();
        }

        yaz::Message message("Hello");
        while (true) {
            publisher.send(std::move(message));
        }

    } else if (mode == "sub") {
        yaz::Socket subscriber(context, ZMQ_SUB, [](auto && /*socket*/, auto && /*message*/) {
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
            publisher.send(std::move(message));
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
