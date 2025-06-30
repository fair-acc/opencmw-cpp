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
    std::uint16_t port     = 8080;
    bool          https    = true;

    for (int i = 1; i < argc; i++) {
        if (std::string_view(argv[i]) == "--port") {
            if (i + 1 < argc) {
                port = static_cast<std::uint16_t>(std::stoi(argv[i + 1]));
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
        return std::format("{}://localhost:{}/{}", scheme, port, pathAndQuery);
    };

    std::println(std::cerr, "Starting {} server for {} on port {}", https ? "HTTPS" : "HTTP", rootPath, port);

    std::println(std::cerr, "Open up {} in your web browser", makeExample("addressbook?contentType=text/html&ctx=FAIR.SELECTOR.ALL"));
    std::println(std::cerr, "Or curl -v -k one of the following:");
    std::println(std::cerr, "'{}'", makeExample("addressbook?contentType=application/json&ctx=FAIR.SELECTOR.ALL"));
    std::println(std::cerr, "'{}'", makeExample("addressbook?contentType=application/json&ctx=FAIR.SELECTOR.ALL"));
    std::println(std::cerr, "'{}'", makeExample("addressbook/addresses?LongPollingIdx=Next"));
    std::println(std::cerr, "'{}'", makeExample("addressbook/addresses?LongPollingIdx=Last"));
    std::println(std::cerr, "'{}'", makeExample("addressbook/addresses?LongPollingIdx=0"));
    std::println(std::cerr, "'{}'", makeExample("beverages/wine?LongPollingIdx=Next"));
    std::println(std::cerr, "'{}'", makeExample("loadTest?topic=1&intervalMs=40&payloadSize=4096&nUpdates=100&LongPollingIdx=Next"));

    // note: inconsistency: brokerName as ctor argument, worker's serviceName as NTTP
    // note: default roles different from java (has: ADMIN, READ_WRITE, READ_ONLY, ANYONE, NULL)
    majordomo::Broker primaryBroker("/PrimaryBroker", testSettings());
    opencmw::query::registerTypes(SimpleContext(), primaryBroker);

    opencmw::majordomo::rest::Settings rest;
    rest.port     = port;
    rest.handlers = { majordomo::rest::cmrcHandler("/assets/*", "", std::make_shared<cmrc::embedded_filesystem>(cmrc::assets::get_filesystem()), "") };
    rest.upgradeHttp3 = true;
    if (https) {
        rest.certificateFilePath = "./demo_public.crt";
        rest.keyFilePath         = "./demo_private.key";
    } else {
        rest.protocols = majordomo::rest::Protocol::Http2;
        std::println(std::cerr, "HTTP/3 disabled, requires TLS");
    }
    if (const auto bound = primaryBroker.bindRest(rest); !bound) {
        std::println("Could not bind REST bridge to port {}: {}", rest.port, bound.error());
        return 1;
    }

    const auto brokerRouterAddress = primaryBroker.bind(URI<>("mds://127.0.0.1:12345"));
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
