#include <majordomo/base64pp.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/LoadTestWorker.hpp>
#include <majordomo/Worker.hpp>

#include "helpers.hpp"

namespace majordomo = opencmw::majordomo;

int main(int argc, char **argv) {
    using opencmw::URI;

    std::string   rootPath = "./";
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

    std::println(std::cerr, "Starting load test server ({}) for {} on port {}", https ? "HTTPS" : "HTTP", rootPath, port);

    opencmw::majordomo::rest::Settings rest;
    rest.port     = port;
    rest.handlers = { majordomo::rest::cmrcHandler("/assets/*", "", std::make_shared<cmrc::embedded_filesystem>(cmrc::assets::get_filesystem()), "") };
    if (https) {
        rest.certificateFilePath = "./demo_public.crt";
        rest.keyFilePath         = "./demo_private.key";
    } else {
        rest.protocols = majordomo::rest::Protocol::Http2;
        std::println(std::cerr, "HTTP/3 disabled, requires TLS");
    }

    majordomo::Broker broker("/Broker", testSettings());
    opencmw::query::registerTypes(opencmw::load_test::Context(), broker);

    if (const auto bound = broker.bindRest(rest); !bound) {
        std::println("Could not bind HTTP/2 REST bridge to port {}: {}", rest.port, bound.error());
        return 1;
    }

    const auto brokerRouterAddress = broker.bind(URI<>("mds://127.0.0.1:12345"));
    if (!brokerRouterAddress) {
        std::cerr << "Could not bind to broker address" << std::endl;
        return 1;
    }

    majordomo::load_test::Worker<"/loadTest"> loadTestWorker(broker);
    RunInThread                               runLoadTest(loadTestWorker);

    broker.run();
}
