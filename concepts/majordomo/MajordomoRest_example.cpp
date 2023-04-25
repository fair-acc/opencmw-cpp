#include <majordomo/base64pp.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Worker.hpp>

#include <atomic>
#include <fstream>
#include <iomanip>
#include <thread>
#include <variant>

#include "helpers.hpp"

namespace majordomo = opencmw::majordomo;

int main(int argc, char **argv) {
    using opencmw::URI;

    std::string rootPath = "./";
    if (argc > 1) {
        rootPath = argv[1];
    }

    std::cerr << "Starting server for " << rootPath << "\n";

    std::cerr << "Open up https://localhost:8080/addressbook?contentType=text/html&ctx=FAIR.SELECTOR.ALL in your web browser\n";
    std::cerr << "Or curl -v -k one of the following:\n";
    std::cerr
            << "'https://localhost:8080/addressbook?contentType=application/json&ctx=FAIR.SELECTOR.ALL'\n"
            << "'https://localhost:8080/addressbook?contentType=application/json&ctx=FAIR.SELECTOR.ALL'\n"
            << "'https://localhost:8080/addressbook/addresses?LongPollingId=Next'\n"
            << "'https://localhost:8080/addressbook/addresses?LongPollingId=0'\n"
            << "'https://localhost:8080/beverages/wine?LongPollingIdx=Subscription'\n";

    // note: inconsistency: brokerName as ctor argument, worker's serviceName as NTTP
    // note: default roles different from java (has: ADMIN, READ_WRITE, READ_ONLY, ANYONE, NULL)
    majordomo::Broker primaryBroker("PrimaryBroker", testSettings());
    opencmw::query::registerTypes(SimpleContext(), primaryBroker);

    auto fs = cmrc::assets::get_filesystem();

    std::variant<
            std::monostate,
            FileServerRestBackend<majordomo::PLAIN_HTTP, decltype(fs)>,
            FileServerRestBackend<majordomo::HTTPS, decltype(fs)>>
            rest;
    if (auto env = ::getenv("DISABLE_REST_HTTPS"); env != nullptr && std::string_view(env) == "1") {
        rest.emplace<FileServerRestBackend<majordomo::PLAIN_HTTP, decltype(fs)>>(primaryBroker, fs, rootPath);
    } else {
        rest.emplace<FileServerRestBackend<majordomo::HTTPS, decltype(fs)>>(primaryBroker, fs, rootPath);
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
    majordomo::Broker secondaryBroker("SecondaryTestBroker", { .dnsAddress = brokerRouterAddress->str() });
    std::jthread      secondaryBrokerThread([&secondaryBroker] {
        secondaryBroker.run();
         });

    //
    majordomo::Worker<"helloWorld", SimpleContext, SimpleRequest, SimpleReply, majordomo::description<"A friendly service saying hello">> helloWorldWorker(primaryBroker, HelloWorldHandler());
    majordomo::Worker<"addressbook", SimpleContext, AddressRequest, AddressEntry>                                                         addressbookWorker(primaryBroker, TestAddressHandler());
    majordomo::Worker<"addressbookBackup", SimpleContext, AddressRequest, AddressEntry>                                                   addressbookBackupWorker(primaryBroker, TestAddressHandler());
    majordomo::BasicWorker<"beverages">                                                                                                   beveragesWorker(primaryBroker, TestIntHandler(10));

    //
    // ImageServiceWorker<"testImage", majordomo::description<"Returns an image">> imageWorker(primaryBroker, std::chrono::seconds(10));

    //
    RunInThread runHelloWorld(helloWorldWorker);
    RunInThread runAddressbook(addressbookWorker);
    RunInThread runAddressbookBackup(addressbookBackupWorker);
    RunInThread runBeverages(beveragesWorker);
    // RunInThread runImage(imageWorker);
    waitUntilServiceAvailable(primaryBroker.context, "addressbook");

    // Fake message publisher - sends messages on notifier.service
    TestNode<majordomo::MdpMessage> publisher(primaryBroker.context);
    publisher.connect(opencmw::majordomo::INTERNAL_ADDRESS_BROKER);

    for (int i = 0; true; ++i) {
        {
            std::cerr << "Sending new number (step " << i << ")\n";
            majordomo::MdpMessage notifyMessage;
            notifyMessage.setTopic("/wine", static_tag);
            notifyMessage.setBody(std::to_string(i), dynamic_tag);

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
                .contentType = opencmw::MIME::JSON
            };

            addressbookWorker.notify("/addressbook", context, entry);

            context.testFilter = "main";
            entry.city         = "London";
            addressbookWorker.notify("/addressbook", context, entry);

            context.testFilter = "alternate";
            entry.city         = "Brighton";
            addressbookWorker.notify("/addressbook", context, entry);
        }

        std::this_thread::sleep_for(3s);
    }
}
