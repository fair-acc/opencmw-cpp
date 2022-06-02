#include <catch2/catch.hpp>

#include <string_view>

#include <URI.hpp>

#include "RestClient.hpp"

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(assets);

namespace opencmw::rest_client_test {

constexpr const char *testCertificate = R"(
R"(
GlobalSign Root CA
==================
-----BEGIN CERTIFICATE-----
MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkGA1UEBhMCQkUx
GTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jvb3QgQ0ExGzAZBgNVBAMTEkds
b2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAwMDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNV
BAYTAkJFMRkwFwYDVQQKExBHbG9iYWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYD
VQQDExJHbG9iYWxTaWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDa
DuaZjc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavpxy0Sy6sc
THAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp1Wrjsok6Vjk4bwY8iGlb
Kk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdGsnUOhugZitVtbNV4FpWi6cgKOOvyJBNP
c1STE4U6G7weNLWLBYy5d4ux2x8gkasJU26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrX
gzT/LCrBbBlDSgeF59N89iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0BAQUF
AAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOzyj1hTdNGCbM+w6Dj
Y1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE38NflNUVyRRBnMRddWQVDf9VMOyG
j/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymPAbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhH
hm4qxFYxldBniYUr+WymXUadDKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveC
X4XSQRjbgbMEHMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==
-----END CERTIFICATE-----
)";

class TestServerCertificates {
    const cmrc::embedded_filesystem fileSystem     = cmrc::assets::get_filesystem();
    const cmrc::file                ca_certificate = fileSystem.open("/assets/ca-cert.pem");
    // server-req.pem -> is usually used to request for the CA signature
    const cmrc::file server_cert = fileSystem.open("/assets/server-cert.pem");
    const cmrc::file server_key  = fileSystem.open("/assets/server-key.pem");
    const cmrc::file client_cert = fileSystem.open("/assets/client-cert.pem");
    const cmrc::file client_key  = fileSystem.open("/assets/client-key.pem");
    const cmrc::file pwd         = fileSystem.open("/assets/password.txt");

public:
    const std::string caCertificate     = { ca_certificate.begin(), ca_certificate.end() };
    const std::string serverCertificate = { server_cert.begin(), server_cert.end() };
    const std::string serverKey         = { server_key.begin(), server_key.end() };
    const std::string clientCertificate = { client_cert.begin(), client_cert.end() };
    const std::string clientKey         = { client_key.begin(), client_key.end() };
    const std::string password          = { pwd.begin(), pwd.end() };
};
inline static const TestServerCertificates testServerCertificates;

TEST_CASE("Basic Rest Client Constructor and API Tests", "[Client]") {
    using namespace opencmw::client;
    RestClient client1;
    REQUIRE(client1.name() == "RestClient");

    RestClient client2(std::make_shared<BasicThreadPool<IO_BOUND>>("RestClient", 1, 10'000));
    REQUIRE(client2.name() == "RestClient");

    RestClient client3("clientName", std::make_shared<BasicThreadPool<IO_BOUND>>("CustomPoolName", 1, 10'000));
    REQUIRE(client3.name() == "clientName");
    REQUIRE(client3.threadPool()->poolName() == "CustomPoolName");

    RestClient client4("clientName");
    REQUIRE(client4.threadPool()->poolName() == "clientName");

    RestClient client5("clientName", DefaultContentTypeHeader(MIME::HTML), MinIoThreads(2), MaxIoThreads(5), ClientCertificates(testCertificate));
    REQUIRE(client5.defaultMimeType() == MIME::HTML);
    REQUIRE(client5.threadPool()->poolName() == "clientName");

    REQUIRE_THROWS_AS(RestClient(ClientCertificates("Invalid Certificate Format")), std::invalid_argument);
}

TEST_CASE("Basic Rest Client Get/Set Test - HTTP", "[Client]") {
    using namespace opencmw::client;
    RestClient client;
    REQUIRE(client.name() == "RestClient");

    httplib::Server server;

    std::string     acceptHeader;
    server.Get("/endPoint", [&acceptHeader](const httplib::Request &req, httplib::Response &res) {
        fmt::print("server received request on path '{}' body = '{}'\n", req.path, req.body);
        if (req.headers.contains("accept")) {
            acceptHeader = req.headers.find("accept")->second;
        } else {
            FAIL("no accept headers found");
        }

        res.set_content("Hello World!", acceptHeader);
    });
    client.threadPool()->execute<"RestServer">([&server] { server.listen("localhost", 8080); });
    while (!server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(server.is_running());

    std::atomic<bool> done(false);
    IoBuffer          data;
    data.put('A');
    data.put('B');
    data.put('C');
    Command command;
    command.command  = mdp::Command::Get;
    command.endpoint = URI<STRICT>("http://localhost:8080/endPoint");
    command.data     = std::move(data);
    command.callback = [&done](const mdp::Message &/*rep*/) {
        done.store(true, std::memory_order_release);
        done.notify_all();
    };
    client.request(command);

    done.wait(false);
    REQUIRE(done.load(std::memory_order_acquire) == true);
    REQUIRE(acceptHeader == MIME::JSON.typeName());
    server.stop();
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
TEST_CASE("Basic Rest Client Get/Set Test - HTTPS", "[Client]") {
    using namespace opencmw::client;
    RestClient client("TestSSLClient", ClientCertificates(testServerCertificates.caCertificate));
    REQUIRE(RestClient::CHECK_CERTIFICATES);
    RestClient::CHECK_CERTIFICATES = true; // 'false' disables certificate check
    REQUIRE(client.name() == "TestSSLClient");
    REQUIRE(client.defaultMimeType() == MIME::JSON);

    // HTTP
    X509       *cert     = opencmw::client::detail::readServerCertificateFromFile(testServerCertificates.serverCertificate);
    EVP_PKEY   *pkey     = opencmw::client::detail::readServerPrivateKeyFromFile(testServerCertificates.serverKey);
    if (const X509_STORE *ca_store = opencmw::client::detail::createCertificateStore(testServerCertificates.caCertificate); !cert || !pkey || !ca_store) {
        FAIL(fmt::format("Failed to load certificate: {}", ERR_error_string(ERR_get_error(), nullptr)));
    }
    httplib::SSLServer server(cert, pkey);

    std::string        acceptHeader;
    server.Get("/endPoint", [&acceptHeader](const httplib::Request &req, httplib::Response &res) {
        if (req.headers.contains("accept")) {
            acceptHeader = req.headers.find("accept")->second;
        } else {
            FAIL("no accept headers found");
        }
        res.set_content("Hello World!", acceptHeader);
    });
    client.threadPool()->execute<"RestServer">([&server] { server.listen("localhost", 8080); });
    while (!server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(server.is_running());

    std::atomic<bool> done(false);
    IoBuffer          data;
    data.put('A');
    data.put('B');
    data.put('C');
    data.put(0);
    Command command;
    command.command  = mdp::Command::Get;
    command.endpoint = URI<STRICT>("https://localhost:8080/endPoint");
    command.data     = std::move(data);
    command.callback = [&done](const mdp::Message &/*rep*/) {
        done.store(true, std::memory_order_release);
        done.notify_all();
    };
    client.request(command);

    done.wait(false);
    REQUIRE(done.load(std::memory_order_acquire) == true);
    REQUIRE(acceptHeader == MIME::JSON.typeName());
    server.stop();
}
#endif

namespace detail {
class EventDispatcher {
    std::mutex              _mutex;
    std::condition_variable _condition;
    std::atomic<int>        _id{ 0 };
    std::atomic<int>        _cid{ -1 };
    std::string             _message;

public:
    void wait_event(httplib::DataSink &sink) {
        std::unique_lock lk(_mutex);
        int              id = _id;
        _condition.wait(lk, [&id, this] { return _cid == id; });
        if (sink.is_writable()) {
            sink.write(_message.data(), _message.size());
        }
    }

    void send_event(const std::string_view &message) {
        std::scoped_lock lk(_mutex);
        _cid     = _id++;
        _message = message;
        _condition.notify_all();
    }
};
} // namespace detail

TEST_CASE("Basic Rest Client Subscribe/Unsubscribe Test", "[Client]") {
    using namespace opencmw::client;

    std::atomic<int>        updateCounter{ 0 };
    detail::EventDispatcher eventDispatcher;
    httplib::Server         server;
    server.Get("/event", [&eventDispatcher, &updateCounter](const httplib::Request &req, httplib::Response &res) {
        auto acceptType = req.headers.find("accept");
        if (acceptType == req.headers.end() || MIME::EVENT_STREAM.typeName() != acceptType->second) { // non-SSE request -> return default response
            res.set_content(fmt::format("update counter = {}", updateCounter), MIME::TEXT);
            return;
        } else {
            fmt::print("server received SSE request on path '{}' body = '{}'\n", req.path, req.body);
            res.set_chunked_content_provider(MIME::EVENT_STREAM, [&eventDispatcher](size_t /*offset*/, httplib::DataSink &sink) {
                eventDispatcher.wait_event(sink);
                return true;
            });
        }
    });
    server.Get("/endPoint", [](const httplib::Request &req, httplib::Response &res) {
        fmt::print("server received request on path '{}' body = '{}'\n", req.path, req.body);
        res.set_content("Hello World!", "text/plain");
    });

    RestClient client;
    client.threadPool()->execute<"RestServer">([&server] { server.listen("localhost", 8080); });
    while (!server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(server.is_running());

    std::atomic<int> receivedRegular(0);
    std::atomic<int> receivedError(0);
    IoBuffer         data;
    data.put('A');
    data.put('B');
    data.put('C');
    data.put(0);

    Command command;
    command.command  = mdp::Command::Subscribe;
    command.endpoint = URI<STRICT>("http://localhost:8080/event");
    command.data     = std::move(data);
    command.callback = [&receivedRegular, &receivedError](const mdp::Message &rep) {
        fmt::print("SSE client received reply = '{}' - body size: '{}'\n", rep.data.asString(), rep.data.size());
        if (rep.error.size() == 0) {
            receivedRegular.fetch_add(1, std::memory_order_relaxed);
        } else {
            receivedError.fetch_add(1, std::memory_order_relaxed);
        }
        receivedRegular.notify_all();
        receivedError.notify_all();
    };

    client.request(command);

    std::cout << "client request launched" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    eventDispatcher.send_event("test-event meta data");
    std::jthread dispatcher([&updateCounter, &eventDispatcher] {
        while (updateCounter < 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            eventDispatcher.send_event(fmt::format("test-event {}", updateCounter++));
        }
    });
    dispatcher.join();

    while (receivedRegular.load(std::memory_order_relaxed) < 5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "done waiting" << std::endl;
    REQUIRE(receivedRegular.load(std::memory_order_acquire) >= 5);

    command.command = mdp::Command::Unsubscribe;
    client.request(command);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "done Unsubscribe" << std::endl;

    client.stop();
    server.stop();
    eventDispatcher.send_event(fmt::format("test-event {}", updateCounter++));
    std::cout << "server stopped" << std::endl;
}

TEST_CASE("Basic Rest Client Long-Polling Test", "[Client]") {
    using namespace opencmw::client;

    httplib::Server   server;
    std::atomic<int>  updateIndex        = -1;
    std::atomic<bool> forceErrorResponse = false;
    auto              updateServerEvent  = [&updateIndex](const int i = 1) {
        updateIndex.fetch_add(i, std::memory_order_relaxed);
        updateIndex.notify_all();
    };
    server.Get("/endPoint", [&server, &updateIndex, &forceErrorResponse](const httplib::Request &req, httplib::Response &res) {
        const std::string s               = req.get_param_value(opencmw::client::LONG_POLLING_IDX_TAG);
        int               reqPollingIndex = std::ranges::any_of(s, ::isdigit) ? std::stoi(s) : -1;
        int               repPollingIndex = -1;
        int               updateIdx       = std::atomic_load_explicit(&updateIndex, std::memory_order_acquire);

        if (reqPollingIndex >= 0 && reqPollingIndex <= updateIdx) { // already published index -> return cached data
            repPollingIndex = reqPollingIndex;
        } else { // no valid long-polling index -> return latest update and/or wait on first one
            repPollingIndex = updateIdx + 1;
        }

        while (repPollingIndex > updateIndex && server.is_running() && !std::atomic_load_explicit(&forceErrorResponse, std::memory_order_acquire)) { // block on read -- wait for new update
            fmt::print("polling idx: requested '{}' -> '{}' >= updateIndex '{}' -- need to block\n", reqPollingIndex, repPollingIndex, updateIdx);
            updateIndex.wait(updateIdx, std::memory_order::acquire);
        }

        if (!server.is_running() || std::atomic_load_explicit(&forceErrorResponse, std::memory_order_acquire)) { // return error
            res.status = 503;                                                                                    // 503: Service Unavailable
            res.set_content(httplib::detail::status_message(res.status), MIME::TEXT);
            return;
        }

        httplib::Params replyQueryParameters = req.params;
        replyQueryParameters.erase(opencmw::client::LONG_POLLING_IDX_TAG);
        replyQueryParameters.insert({ opencmw::client::LONG_POLLING_IDX_TAG, repPollingIndex >= 0 ? fmt::format("{}", repPollingIndex + 1) : "0" });
        auto redirectPath = fmt::format("{}?{}", req.path, httplib::detail::params_to_query_str(replyQueryParameters));
        res.set_content(fmt::format("response body for idx={}", repPollingIndex), MIME::TEXT);
        res.set_redirect(redirectPath);
    });

    RestClient client;
    client.threadPool()->execute<"RestServer">([&server] { server.listen("localhost", 8080); });
    while (!server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(server.is_running());

    std::atomic<int> receivedRegular(0);
    std::atomic<int> receivedError(0);
    Command          command;
    command.command  = mdp::Command::Subscribe;
    command.endpoint = URI<STRICT>(fmt::format("http://localhost:8080/endPoint?{}", opencmw::client::LONG_POLLING_IDX_TAG));
    command.callback = [&receivedRegular, &receivedError](const mdp::Message &rep) {
        fmt::print("Long-Polling client received reply = '{}' - body size: {} error: '{}' size: {}\n", rep.data.asString(), rep.data.size(), rep.error, rep.error.size());
        if (rep.error.size() == 0) {
            receivedRegular.fetch_add(1, std::memory_order_relaxed);
        } else {
            receivedError.fetch_add(1, std::memory_order_relaxed);
        }
        receivedRegular.notify_all();
        receivedError.notify_all();
    };
    client.request(command);
    std::cout << "client request launched" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(receivedRegular.load(std::memory_order_acquire) == 0);
    REQUIRE(receivedError.load(std::memory_order_acquire) == 0);
    updateServerEvent(1);
    while (receivedRegular.load(std::memory_order_relaxed) < 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(receivedRegular.load(std::memory_order_acquire) == 1);
    REQUIRE(receivedError.load(std::memory_order_acquire) == 0);
    updateServerEvent(4);

    while (receivedRegular.load(std::memory_order_relaxed) < 5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(receivedRegular.load(std::memory_order_acquire) == 5);
    REQUIRE(receivedError.load(std::memory_order_acquire) == 0);

    forceErrorResponse.store(true);
    updateServerEvent(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(receivedRegular.load(std::memory_order_acquire) == 5);
    REQUIRE(receivedError.load(std::memory_order_acquire) == 1);

    command.command = mdp::Command::Unsubscribe;
    client.request(command);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    client.stop();
    updateIndex.fetch_add(1, std::memory_order_relaxed);
    updateServerEvent(1); // to wake-up potential wait (N.B. only relevant for unit-test)
    server.stop();
    std::cout << "server stopped" << std::endl;
}

} // namespace opencmw::rest_client_test
