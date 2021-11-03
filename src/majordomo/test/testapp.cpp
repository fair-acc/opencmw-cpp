#include <iostream>
#include <unordered_map>

#include <majordomo/broker.hpp>
#include <majordomo/client.hpp>
#include <majordomo/BasicMdpWorker.hpp>

#include <fmt/format.h>

constexpr auto property_store_service = "property_store";

using Majordomo::OpenCMW::MdpMessage;

class TestWorker : public Majordomo::OpenCMW::BasicMdpWorker {

    std::unordered_map<std::string, std::string> _properties;

public:
    explicit TestWorker(yaz::Context &context)
        : BasicMdpWorker(context, property_store_service) {
    }

    std::optional<Majordomo::OpenCMW::MdpMessage> handle_get(Majordomo::OpenCMW::MdpMessage &&message) override {
        message.setWorkerCommand(MdpMessage::WorkerCommand::Final);

        const auto property = std::string(message.body());
        const auto it = _properties.find(property);

        if (it != _properties.end()) {
            message.setBody(it->second, yaz::MessagePart::dynamic_bytes_tag{});
            message.setError("", yaz::MessagePart::static_bytes_tag{});
        } else {
            message.setBody("", yaz::MessagePart::static_bytes_tag{});
            message.setError(fmt::format("Unknown property '{}'", property), yaz::MessagePart::dynamic_bytes_tag{});
        }

        return std::move(message);
    }

    std::optional<Majordomo::OpenCMW::MdpMessage> handle_set(Majordomo::OpenCMW::MdpMessage &&message) override {
        message.setWorkerCommand(MdpMessage::WorkerCommand::Final);

        const auto request = message.body();

        const auto pos = request.find("=");

        if (pos != std::string_view::npos) {
            const auto property = request.substr(0, pos);
            const auto value = request.substr(pos + 1, request.size() - 1 - pos);

            _properties[std::string(property)] = value;

            std::cout << fmt::format("Property '{}' set to value '{}'", property, value) << std::endl;

            message.setBody(fmt::format("Property '{}' set to value '{}'", property, value), yaz::MessagePart::dynamic_bytes_tag{});
            message.setError("", yaz::MessagePart::static_bytes_tag{});
        } else {
            message.setBody("", yaz::MessagePart::static_bytes_tag{});
            message.setError("Invalid request, \"property=value\" expected", yaz::MessagePart::static_bytes_tag{});
        }

        return std::move(message);
    }
};

int main(int argc, char ** argv) {
    if (argc < 2) {
        std::cerr << "Usage: majordomo_testapp <broker|client|worker> <options>\n\n"
                     "Examples:\n\n"
                     " Run Broker at tcp://127.0.0.1:12345 (Note: localhost does not work!):\n"
                     "    majordomo_testapp broker tcp://127.0.0.1:12345\n\n"
                     " Connect worker to tcp://127.0.0.1:12345 (Note: mdp:// does not work right now):\n"
                     "    majordomo_testapp worker tcp://127.0.0.1:12345\n\n"
                     " Via a client, set property 'foo' to 'bar':\n"
                     "    majordomo_testapp client tcp://127.0.0.1:12345 set foo bar\n\n"
                     " Via a client, read the value of the property 'foo':\n"
                     "    majordomo_testapp client tcp://127.0.0.1:12345 get foo\n\n";
        return 1;
    }

    const std::string_view mode = argv[1];

    if (mode == "broker") {
        if (argc < 3) {
            std::cerr << "Usage: majordomo_testapp broker <router_endpoint> [<pub_endpoint>]\n";
            return 1;
        }
        const std::string_view router_endpoint = argv[2];
        const std::string_view pub_endpoint = argc > 3 ? argv[3] : "";

        yaz::Context context;
        Majordomo::OpenCMW::Broker broker("test_broker", "", context);
        const auto router_address = broker.bind(router_endpoint);
        if (!router_address) {
            std::cerr << fmt::format("Could not bind to '{}'\n", router_endpoint);
            return 1;
        }

        if (pub_endpoint.empty()) {
            std::cout << fmt::format("Listening to '{}' (ROUTER)\n", *router_address);
        } else {
            const auto pub_address = broker.bind(pub_endpoint);
            if (!pub_address) {
                std::cerr << fmt::format("Could not bind to '{}'\n", router_endpoint);
                return 1;
            }

            std::cout << fmt::format("Listening to '{}' (ROUTER) and '{}' (PUB)\n", *router_address, *pub_address);
        }

        broker.run();
        return 0;
    }

    if (mode == "worker") {
        if (argc < 3) {
            std::cerr << "Usage: majordomo_testapp worker <broker_address>\n";
            return 1;
        }
        const std::string_view broker_address = argv[2];

        yaz::Context context;
        TestWorker worker(context);

        if (!worker.connect(broker_address)) {
            std::cerr << fmt::format("Could not connect to broker at '{}'\n", broker_address);
            return 1;
        }

        worker.run();
        return 0;
    }

    if (mode == "client") {
        if (argc < 4) {
            std::cerr << "Usage: majordomo_testapp client <broker_address> set|get <options>\n";
            return 1;
        }

        const std::string_view broker_address = argv[2];
        const std::string_view command = argv[3];

        if (command == "get" && argc != 5) {
            std::cerr << "Usage: majordomo_testapp client <broker_address> get <property>\n";
            return 1;
        }

        if (command == "set" && argc != 6) {
            std::cerr << "Usage: majordomo_testapp client <broker_address> set <property> <value>\n";
            return 1;
        }

        const std::string_view property = argv[4];
        const std::string_view value = argc == 6 ? argv[5] : "";

        yaz::Context context;
        Majordomo::OpenCMW::Client client(context);
        if (!client.connect(broker_address)) {
            std::cerr << fmt::format("Could not connect to broker at '{}'\n", broker_address);
            return 1;
        }

        bool reply_received = false;

        if (command == "set") {
            client.set(property_store_service, fmt::format("{}={}", property, value), [&reply_received](auto &&reply) {
                reply_received = true;
                if (!reply.error().empty()) {
                    std::cout << "Error: " << reply.error() << std::endl;
                    return;
                }

                std::cout << reply.body() << std::endl;
            });
        } else {
            client.get(property_store_service, property, [property, &reply_received](auto &&reply) {
                reply_received = true;
                if (!reply.error().empty()) {
                    std::cout << "Error: " << reply.error() << std::endl;
                    return;
                }

                std::cout << fmt::format("The value of '{}' is '{}'\n", property, reply.body());
            });
        }

        while (!reply_received) {
            client.read();
        }
        return 0;
    }
}
