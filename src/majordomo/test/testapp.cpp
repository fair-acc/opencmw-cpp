#include <iostream>
#include <thread>
#include <unordered_map>

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Client.hpp>

#include <fmt/format.h>

using opencmw::majordomo::Command;
using opencmw::majordomo::Context;
using opencmw::majordomo::MdpMessage;
using opencmw::majordomo::MessageFrame;
using opencmw::majordomo::RequestContext;
using opencmw::majordomo::Settings;

static Settings testSettings() {
    return Settings{}; // use defaults
}

class TestHandler {
    std::unordered_map<std::string, std::string> _properties;

public:
    void handleRequest(RequestContext &context) {
        if (context.request.command() == Command::Get) {
            const auto property = std::string(context.request.body());
            const auto it       = _properties.find(property);

            if (it != _properties.end()) {
                context.reply->setBody(it->second, MessageFrame::dynamic_bytes_tag{});
            } else {
                context.reply->setError(fmt::format("Unknown property '{}'", property), MessageFrame::dynamic_bytes_tag{});
            }
            return;
        }

        assert(context.request.command() == Command::Set);

        const auto request = context.request.body();
        const auto pos     = request.find("=");

        if (pos != std::string_view::npos) {
            const auto property                = request.substr(0, pos);
            const auto value                   = request.substr(pos + 1, request.size() - 1 - pos);

            _properties[std::string(property)] = value;

            std::cout << fmt::format("Property '{}' set to value '{}'", property, value) << std::endl;

            context.reply->setBody(fmt::format("Property '{}' set to value '{}'", property, value), MessageFrame::dynamic_bytes_tag{});
        } else {
            context.reply->setError("Invalid request, \"property=value\" expected", MessageFrame::static_bytes_tag{});
        }
    }
};

int main(int argc, char **argv) {
    using opencmw::majordomo::BasicMdpWorker;
    using opencmw::majordomo::Broker;
    using opencmw::majordomo::Settings;

    static constexpr auto propertyStoreService = "property_store";

    if (argc < 2) {
        std::cerr << "Usage: majordomo_testapp <broker|client|worker|brokerworker> <options>\n\n"
                     "Examples:\n\n"
                     " Run broker at tcp://127.0.0.1:12345 (Note: localhost does not work!):\n"
                     "    majordomo_testapp broker tcp://127.0.0.1:12345\n\n"
                     " Connect worker to tcp://127.0.0.1:12345 (Note: mdp:// does not work right now):\n"
                     "    majordomo_testapp worker tcp://127.0.0.1:12345\n\n"
                     " Run broker and worker in the same process, reachable via tcp://127.0.0.1:12345:\n\n"
                     "    majordomo_testapp brokerworker tcp://127.0.0.1:12345\n\n"
                     " Via a client, set property 'foo' to 'bar':\n"
                     "    majordomo_testapp client tcp://127.0.0.1:12345 set foo bar\n\n"
                     " Via a client, read the value of the property 'foo':\n"
                     "    majordomo_testapp client tcp://127.0.0.1:12345 get foo\n\n";
        return 1;
    }

    const std::string_view mode = argv[1];

    if (mode == "broker" || mode == "brokerworker") {
        if (argc < 3) {
            std::cerr << "Usage: majordomo_testapp broker <routerEndpoint> [<pubEndpoint>]\n";
            return 1;
        }
        const std::string_view routerEndpoint = argv[2];
        const std::string_view pubEndpoint    = argc > 3 ? argv[3] : "";

        Context                context;
        Broker                 broker("test_broker", {}, testSettings());
        const auto             routerAddress = broker.bind(routerEndpoint);
        if (!routerAddress) {
            std::cerr << fmt::format("Could not bind to '{}'\n", routerEndpoint);
            return 1;
        }

        if (pubEndpoint.empty()) {
            std::cout << fmt::format("Listening to '{}' (ROUTER)\n", *routerAddress);
        } else {
            const auto pubAddress = broker.bind(pubEndpoint);
            if (!pubAddress) {
                std::cerr << fmt::format("Could not bind to '{}'\n", routerEndpoint);
                return 1;
            }

            std::cout << fmt::format("Listening to '{}' (ROUTER) and '{}' (PUB)\n", *routerAddress, *pubAddress);
        }

        if (mode == "broker") {
            broker.run();
            return 0;
        }

        BasicMdpWorker worker(propertyStoreService, broker, TestHandler{});

        auto           brokerThread = std::jthread([&broker] {
            broker.run();
        });

        auto           workerThread = std::jthread([&worker] {
            worker.run();
        });

        brokerThread.join();
        workerThread.join();
        return 0; // never reached
    }

    if (mode == "worker") {
        if (argc < 3) {
            std::cerr << "Usage: majordomo_testapp worker <brokerAddress>\n";
            return 1;
        }
        const std::string_view brokerAddress = argv[2];

        BasicMdpWorker         worker(propertyStoreService, brokerAddress, TestHandler{});

        worker.run();
        return 0;
    }

    if (mode == "client") {
        if (argc < 4) {
            std::cerr << "Usage: majordomo_testapp client <brokerAddress> set|get <options>\n";
            return 1;
        }

        const std::string_view brokerAddress = argv[2];
        const std::string_view command       = argv[3];

        if (command == "get" && argc != 5) {
            std::cerr << "Usage: majordomo_testapp client <brokerAddress> get <property>\n";
            return 1;
        }

        if (command == "set" && argc != 6) {
            std::cerr << "Usage: majordomo_testapp client <brokerAddress> set <property> <value>\n";
            return 1;
        }

        const std::string_view     property = argv[4];
        const std::string_view     value    = argc == 6 ? argv[5] : "";

        Context                    context;
        opencmw::majordomo::Client client(context);
        if (!client.connect(brokerAddress)) {
            std::cerr << fmt::format("Could not connect to broker at '{}'\n", brokerAddress);
            return 1;
        }

        bool replyReceived = false;

        if (command == "set") {
            client.set(propertyStoreService, fmt::format("{}={}", property, value), [&replyReceived](auto &&reply) {
                replyReceived = true;
                if (!reply.error().empty()) {
                    std::cout << "Error: " << reply.error() << std::endl;
                    return;
                }

                std::cout << reply.body() << std::endl;
            });
        } else {
            client.get(propertyStoreService, property, [property, &replyReceived](auto &&reply) {
                replyReceived = true;
                if (!reply.error().empty()) {
                    std::cout << "Error: " << reply.error() << std::endl;
                    return;
                }

                std::cout << fmt::format("The value of '{}' is '{}'\n", property, reply.body());
            });
        }

        while (!replyReceived) {
            client.tryRead();
        }
        return 0;
    }

    std::cerr << fmt::format("Unknown mode '{}'\n", mode);
    return 1;
}
