#include <majordomo/base64pp.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/LoadTestWorker.hpp>
#include <majordomo/Worker.hpp>

#include <thread>

#include "helpers.hpp"

namespace majordomo = opencmw::majordomo;
namespace mdp       = opencmw::mdp;

int main(int argc, char **argv) {
    using opencmw::URI;

    std::string rootPath = "./";
    uint16_t port  = 8080;
    bool     https = true;

    for (int i = 1; i < argc; i++) {
        fmt::println(std::cerr, "argv[{}]: '{}'", i, argv[i]);
        if (std::string_view(argv[i]) == "--port") {
            if (i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[i + 1]));
                ++i;
                continue;
            }
        } else if (std::string_view(argv[i]) == "--http") {
            https = false;
        } else {
            rootPath = argv[i];
        }
    }

    const auto scheme      = https ? "https" : "http";
    auto       makeExample = [scheme, port](std::string_view pathAndQuery) {
        return fmt::format("{}://localhost:{}/{}", scheme, port, pathAndQuery);
    };

    fmt::println(std::cerr, "Starting {} server for {} on port {}", https ? "HTTPS" : "HTTP", rootPath, port);

    fmt::println(std::cerr, "Open up {} in your web browser", makeExample("addressbook?contentType=text/html&ctx=FAIR.SELECTOR.ALL"));
    fmt::println(std::cerr, "Or curl -v -k one of the following:");
    fmt::println(std::cerr, "'{}'", makeExample("addressbook?contentType=application/json&ctx=FAIR.SELECTOR.ALL"));
    fmt::println(std::cerr, "'{}'", makeExample("addressbook?contentType=application/json&ctx=FAIR.SELECTOR.ALL"));
    fmt::println(std::cerr, "'{}'", makeExample("addressbook/addresses?LongPollingIdx=Next"));
    fmt::println(std::cerr, "'{}'", makeExample("addressbook/addresses?LongPollingIdx=Last"));
    fmt::println(std::cerr, "'{}'", makeExample("addressbook/addresses?LongPollingIdx=FirstAvailable"));
    fmt::println(std::cerr, "'{}'", makeExample("addressbook/addresses?LongPollingIdx=0"));
    fmt::println(std::cerr, "'{}'", makeExample("beverages/wine?LongPollingIdx=Next"));

    // note: inconsistency: brokerName as ctor argument, worker's serviceName as NTTP
    // note: default roles different from java (has: ADMIN, READ_WRITE, READ_ONLY, ANYONE, NULL)
    majordomo::Broker primaryBroker("/PrimaryBroker", testSettings());
    opencmw::query::registerTypes(SimpleContext(), primaryBroker);
    opencmw::query::registerTypes(majordomo::load_test::Context(), primaryBroker);

    opencmw::majordomo::rest::Settings rest;
    rest.port     = port;
    rest.handlers = { majordomo::rest::cmrcHandler("/assets/", std::make_shared<cmrc::embedded_filesystem>(cmrc::assets::get_filesystem()), "/assets/") };
    if (https) {
        rest.certificateFilePath = "./demo_public.crt";
        rest.keyFilePath         = "./demo_private.key";
    }
    if (const auto bound = primaryBroker.bindRest(rest); !bound) {
        fmt::println("Could not bind HTTP/2 REST bridge to port {}: {}", rest.port, bound.error());
        return 1;
    }

    const auto brokerRouterAddress = primaryBroker.bind(URI<>("mds://127.0.0.1:44444"));
    if (!brokerRouterAddress) {
        std::cerr << "Could not bind to broker address" << std::endl;
        return 1;
    }

    // note: our thread handling is very verbose, offer nicer API
    std::jthread primaryBrokerThread([&primaryBroker] {
        primaryBroker.run();
    });

    // second broker to test DNS functionalities
    majordomo::Broker secondaryBroker("/SecondaryTestBroker", { .dnsAddress = brokerRouterAddress->str() });
    std::jthread      secondaryBrokerThread([&secondaryBroker] {
        secondaryBroker.run();
    });

    //
    majordomo::Worker<"/helloWorld", SimpleContext, SimpleRequest, SimpleReply, majordomo::description<"A friendly service saying hello">> helloWorldWorker(primaryBroker, HelloWorldHandler());
    majordomo::Worker<"/addressbook", SimpleContext, AddressRequest, AddressEntry>                                                         addressbookWorker(primaryBroker, TestAddressHandler());
    majordomo::Worker<"/addressbookBackup", SimpleContext, AddressRequest, AddressEntry>                                                   addressbookBackupWorker(primaryBroker, TestAddressHandler());
    majordomo::BasicWorker<"/beverages">                                                                                                   beveragesWorker(primaryBroker, TestIntHandler(10));
    majordomo::load_test::Worker<"/loadTest">                                                                                              loadTestWorker(primaryBroker);

    //
    ImageServiceWorker<"/testImage", majordomo::description<"Returns an image">> imageWorker(primaryBroker, std::chrono::seconds(10));

    //
    RunInThread runHelloWorld(helloWorldWorker);
    RunInThread runAddressbook(addressbookWorker);
    RunInThread runAddressbookBackup(addressbookBackupWorker);
    RunInThread runBeverages(beveragesWorker);
    RunInThread runLoadTest(loadTestWorker);
    RunInThread runImage(imageWorker);
    waitUntilWorkerServiceAvailable(primaryBroker.context, addressbookWorker);

    // Fake message publisher - sends messages on notifier.service
    TestNode<mdp::MessageFormat::WithoutSourceId> publisher(primaryBroker.context);
    publisher.connect(majordomo::INTERNAL_ADDRESS_BROKER);

    for (int i = 0; true; ++i) {
        {
            std::cerr << "Sending new number (step " << i << ")\n";
            mdp::Message notifyMessage;
            notifyMessage.topic = mdp::Message::URI("/wine");
            const auto data     = std::to_string(i);
            notifyMessage.data  = opencmw::IoBuffer(data.data(), data.size());

            beveragesWorker.notify(std::move(notifyMessage));
        }

        {
            AddressEntry entry{
                .name         = "Sherlock Holmes",
                .street       = "Baker Street",
                .streetNumber = i,
                .postalCode   = "1000",
                .city         = "London",
                .isCurrent    = true
            };

            SimpleContext context{
                .ctx         = opencmw::TimingCtx(),
                .testFilter  = ""s,
                .contentType = opencmw::MIME::JSON
            };

            addressbookWorker.notify(context, entry);

            context.testFilter = "main";
            entry.city         = "London";
            addressbookWorker.notify(context, entry);

            context.testFilter = "alternate";
            entry.city         = "Brighton";
            addressbookWorker.notify(context, entry);
        }

        std::this_thread::sleep_for(3s);
    }
}
