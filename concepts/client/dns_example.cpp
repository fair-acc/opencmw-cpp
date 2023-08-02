#include <IoSerialiserYaS.hpp>
#include <services/dns_client.hpp>
#include <services/dns.hpp>
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

#include <string_view>
#include <thread>

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
    auto                                                        fs = cmrc::assets::get_filesystem();
    majordomo::RestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest_backend{ broker, fs, URI<>{ std::string{ httpAddress } } };
    DnsWorkerType                                               dnsWorker{ broker, DnsHandler{} };
    broker.bind(URI<>{ std::string{ mdpAddress } }, majordomo::BindOption::Router);

    RunInThread restThread(rest_backend);
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
    client.registerSignalsAsync([](const auto &/*entries*/) {
        fmt::print("registered signal\n");
    }, {entry_a});
}

void query_devices(auto &client, std::string_view query) {
    Entry query_filter{ .signal_name = std::string{ query } };

    std::promise<std::vector<Entry>> promise;
    /*client.querySignalsAsync([](std::vector<Entry> entries) {
        std::cout << "result size:" << entries.size() << std::endl;
          });*/
/*    client.querySignalsFuture(promise);
    auto f = promise.get_future();
    while (f.wait_for(std::chrono::seconds{1}) != std::future_status::ready) {
        DEBUG_LOG("ohh, yeah");
#ifdef EMSCRIPTEN
//        emscripten_current_thread_process_queued_calls();
        emscripten_thread_sleep(500);
#endif
    }
    try {
        std::cout << f.get().size() << std::endl;
    } catch (...) {
        std::cout << "future oopos" << std::endl;
    }*/
    DEBUG_LOG("ALWAYS")
    auto s = client.querySignals();
    //auto s = client.querySignalsSyncFut();
    DEBUG_LOG("NEVER")
    DEBUG_LOG(s);

    /*client.querySignalsAsync([](const auto &entries) {
        fmt::print("got {} results:\n", entries.size());
        for (auto &entry : entries) {
            fmt::print("- {}\n", entry);
        }
    }, query_filter);*/
}
#ifdef EMSCRIPTEN
void spin_once() {
    static int i = 0;
    if (i++ == 2) {
        emscripten_force_exit(0);
    }
}
#endif

int main(int argc, char *argv[]) {
#ifdef EMSCRIPTEN
    emscripten_trace_configure_for_google_wtf();
#endif // EMSCRIPTEN
    using opencmw::URI;
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    std::string_view                    command = args[0];
    if (command == "server" && args.size() == 3 ) {
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
//        DnsRestClient         dns_client{ std::string{ args[1] } };
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
#ifndef __EMSCRIPTEN__
        clientContext.stop();
#endif
    } else {
        fmt::print("not enough arguments: {}\n", args);
    }

#ifdef EMSCRIPTEN
#ifdef PROXY_TO_PTHREAD
    emscripten_pause_main_loop();
#endif
    emscripten_current_thread_process_queued_calls();
    //emscripten_runtime_keepalive_push();
    //emscripten_cancel_main_loop();
    //int ret = 0;
    //emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI, exit, 0);
    emscripten_set_main_loop(spin_once, 30, 0);
#endif // EMSCRIPTEN
}
