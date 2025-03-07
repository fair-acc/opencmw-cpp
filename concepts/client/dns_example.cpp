#include <IoSerialiserYaS.hpp>
#include <services/dns.hpp>
#include <services/dns_client.hpp>
#include <utility>
#ifndef EMSCRIPTEN
#include <Client.hpp>
#include <concepts/majordomo/helpers.hpp>
#include <majordomo/base64pp.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>
#else
#include <emscripten/trace.h>
#endif // EMSCRIPTEN

#include <RestClient.hpp>

#include <string_view>

using namespace std::chrono_literals;
using namespace opencmw;
using namespace opencmw::service::dns;

/**
 * Small Sample application to test dns especially between different build types
 * dns_example server http://0.0.0.0:8053                  -> run the dns server on localhost http port 8053
 * dns_example register http://localhost:8053 testSignal   -> add a sample signal named test Signal
 * dns_example query http://localhost:8053 testSignal       -> query all signals whose signalName matches the argument
 *
 * The server part is not available on emscripten
 */

#ifndef EMSCRIPTEN
void run_dns_server(std::string_view httpAddress, std::string_view mdpAddress) {
    majordomo::Broker<>                                         broker{ "Broker", {} };
    std::string                                                 rootPath{ "./" };
    majordomo::rest::Settings                                   rest;
    rest.handlers = { majordomo::rest::cmrcHandler("/assets/*", std::make_shared<cmrc::embedded_filesystem>(cmrc::assets::get_filesystem()), "/assets/") };

    if (const auto bound = broker.bindRest(rest); !bound) {
        fmt::println(std::cerr, "failed to bind REST: {}", bound.error());
        std::exit(1);
        return;
    }
    DnsWorkerType                                               dnsWorker{ broker, DnsHandler{} };
    broker.bind(URI<>{ std::string{ mdpAddress } }, majordomo::BindOption::Router);

    RunInThread dnsThread(dnsWorker);
    RunInThread brokerThread(broker);

    fmt::print("DNS service running, press ENTER to terminate\n");
    getchar();

    // cleanup tmp file
    auto filename = "dns_data_storage.yas";
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }
}
#endif

void register_device(auto &client, std::string_view signal) {
    Entry entry_a{ .protocol = "http", .hostname = "test.example.com", .port = 1337, .service_name = "test", .service_type = "", .signal_name = std::string{ signal }, .signal_unit = "", .signal_rate = 1e3, .signal_type = "" };
    std::cout << "registered signal: " << client.registerSignal(entry_a) << std::endl;
}

void query_devices(auto &client, std::string_view query) {
    Entry query_filter{ .signal_name = std::string{ query } };
    auto  signals = client.querySignals(query_filter);
    std::cout << "found signal: " << signals << std::endl;
}

int main(int argc, char *argv[]) {
    using opencmw::URI;
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    std::string_view                    command = args[0];
    if (command == "server" && args.size() == 3) {
#if defined(EMSCRIPTEN)
        fmt::print("unable to run server on emscripten\n");
#else
        fmt::print("running server on addresses: {}, {}\n", args[1], args[2]);
        run_dns_server(args[1], args[2]);
#endif
    } else if (args.size() >= 2) {
        // get client
        fmt::print("getting client for server {}\n", args[1]);
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
#ifndef EMSCRIPTEN
        zmq::Context context;
        clients.emplace_back(std::make_unique<client::MDClientCtx>(context, 20ms, "dnsTestClient"));
#endif
        clients.emplace_back(std::make_unique<client::RestClient>(opencmw::client::DefaultContentTypeHeader(MIME::BINARY)));

        client::ClientContext clientContext{ std::move(clients) };
        DnsClient             dns_client{ clientContext, URI<>{ std::string{ args[1] } } };

        if (command == "register" && args.size() == 3) {
            fmt::print("registering example device {}\n", args[2]);
            register_device(dns_client, args[2]);
        } else if (command == "query" && args.size() == 3) {
            fmt::print("querying devices: {}\n", args[2]);
            query_devices(dns_client, args[2]);
        } else {
            fmt::print("unknown command: {}\n", command);
        }
        fmt::print("stopping client\n");

        clientContext.stop();

    } else {
        fmt::print("not enough arguments: {}\n", args);
    }
}
