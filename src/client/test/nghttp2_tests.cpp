#include <majordomo/RestServer.hpp>
#include <RestClient.hpp>

#include "zmq.h"
#include <zmq/ZmqUtils.hpp>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(assets);

#include <catch2/catch.hpp>

#include <chrono>
#include <stop_token>
#include <sys/types.h>

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

using namespace opencmw;
using namespace opencmw::majordomo::detail::rest;
using opencmw::URI;

constexpr uint16_t kServerPort = 33339;

void               ensureMessageReceived(RestServer &server, std::stop_token stopToken, std::deque<Message> &messages, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto start = std::chrono::system_clock::now();
    while (!stopToken.stop_requested() && std::chrono::system_clock::now() - start < timeout) {
        if (!messages.empty()) {
            return;
        }

        std::vector<zmq_pollitem_t> pollerItems;
        server.populatePollerItems(pollerItems);

        const int rc = zmq_poll(pollerItems.data(), static_cast<int>(pollerItems.size()), 500);

        if (rc == 0) {
            continue;
        }

        for (const auto &item : pollerItems) {
            auto ms = server.processReadWrite(item.fd, item.revents & ZMQ_POLLIN, item.revents & ZMQ_POLLOUT);
            messages.insert(messages.end(), ms.begin(), ms.end());
        }
    }
};

bool waitFor(std::atomic<int> &responseCount, int expected, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto start = std::chrono::system_clock::now();
    while (responseCount.load() < expected && std::chrono::system_clock::now() - start < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (responseCount.load() < expected) {
        FAIL(std::format("Expected {} responses, but got only {}\n", expected, responseCount.load()));
    }
    return responseCount.load() == expected;
}

struct Stopper {
    std::stop_source source;
    explicit Stopper(std::stop_source s)
        : source(s) {}
    ~Stopper() {
        source.request_stop();
    }
};

static std::string normalize(URI<> uri) {
    return opencmw::mdp::Topic::fromMdpTopic(uri).toZmqTopic();
}

TEST_CASE("Basic Client Constructor and API Tests", "[http2]") {
    using namespace opencmw::client;

    RestClient client1;
    REQUIRE(client1.defaultMimeType() == MIME::JSON);
    REQUIRE(client1.verifySslPeers() == true);

    auto client2 = RestClient(DefaultContentTypeHeader(MIME::HTML), ClientCertificates(testCertificate));
    REQUIRE(client2.defaultMimeType() == MIME::HTML);
    REQUIRE(client2.verifySslPeers() == true);

    RestClient client3(DefaultContentTypeHeader(MIME::BINARY), VerifyServerCertificates(false));
    REQUIRE(client3.defaultMimeType() == MIME::BINARY);
    REQUIRE(client3.verifySslPeers() == false);
}

TEST_CASE("GET HTTP", "[http2]") {
    using namespace opencmw::client;

    auto serverThread = std::jthread([](std::stop_token stopToken) {
        RestServer server;
        REQUIRE(server.bind(kServerPort, majordomo::rest::Http2));

        std::deque<Message> messages;
        ensureMessageReceived(server, stopToken, messages);
        REQUIRE(messages.size() >= 1);
        const auto req0 = std::move(messages[0]);
        messages.pop_front();
        REQUIRE(req0.command == mdp::Command::Get);
        REQUIRE(req0.topic.path() == "/sayhello");
        REQUIRE(req0.error.empty());
        REQUIRE(req0.data.empty());

        Message reply0;
        reply0.command         = mdp::Command::Final;
        reply0.clientRequestID = req0.clientRequestID;
        reply0.topic           = URI<>("/sayhello");
        reply0.data            = opencmw::IoBuffer("Hello, World!");
        server.handleResponse(std::move(reply0));

        ensureMessageReceived(server, stopToken, messages);
    });

    // Client using plain http
    RestClient  http;
    Stopper     stopper(serverThread.get_stop_source());
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // give the server some time to start listening

    std::atomic<int> responseCount = 0;

    client::Command  req0;
    req0.command         = mdp::Command::Get;
    req0.clientRequestID = opencmw::IoBuffer("0");
    req0.topic           = URI<>(std::format("http://localhost:{}/sayhello?client=http", kServerPort));
    req0.callback        = [&responseCount](const mdp::Message &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == "Hello, World!");
        REQUIRE(msg.clientRequestID.asString() == "0");
        REQUIRE(msg.topic.path() == "/sayhello");
        responseCount++;
    };
    http.request(std::move(req0));

    // Client that verifies the server's certificate and trusts its CA
    RestClient      https(ClientCertificates(testServerCertificates.caCertificate));
    client::Command req1;
    req1.command         = mdp::Command::Get;
    req1.topic           = URI<>(std::format("https://localhost:{}/sayhello?client=https", kServerPort));
    req1.clientRequestID = opencmw::IoBuffer("0");
    req1.callback        = [&responseCount](const mdp::Message &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.protocolName == "https");
        REQUIRE(msg.error.contains("Could not connect to endpoint"));
        REQUIRE(msg.data.asString() == "");
        REQUIRE(msg.clientRequestID.asString() == "0");
        REQUIRE(msg.topic.path() == "/sayhello");
        responseCount++;
    };
    https.request(std::move(req1));

    REQUIRE(waitFor(responseCount, 2));
}

TEST_CASE("HTTPS", "[http2]") {
    using namespace opencmw::client;

    auto        serverThread = std::jthread([&](std::stop_token stopToken) {
        auto server = RestServer::sslWithBuffers(testServerCertificates.serverCertificate, testServerCertificates.serverKey);
        if (!server) {
            FAIL(std::format("Failed to create server: {}", server.error()));
            return;
        }

        REQUIRE(server->bind(kServerPort, majordomo::rest::Http2 | majordomo::rest::Http3));

        std::deque<Message> messages;
        for (int i = 0; i < 2; i++) {
            ensureMessageReceived(server.value(), stopToken, messages);
            REQUIRE(messages.size() >= 1);
            const auto req0 = std::move(messages[0]);
            messages.pop_front();
            REQUIRE(req0.command == mdp::Command::Get);
            REQUIRE(req0.topic.path() == "/sayhello");
            REQUIRE(req0.error.empty());
            REQUIRE(req0.data.empty());

            Message reply0;
            reply0.command         = mdp::Command::Final;
            reply0.clientRequestID = req0.clientRequestID;
            reply0.topic           = URI<>("/sayhello");
            reply0.data            = opencmw::IoBuffer("Hello, World!");
            server->handleResponse(std::move(reply0));
        }

        const auto start = std::chrono::system_clock::now();
        while (!stopToken.stop_requested() && std::chrono::system_clock::now() - start < std::chrono::seconds(5)) {
            ensureMessageReceived(server.value(), stopToken, messages);
        }
    });

    Stopper     stopper(serverThread.get_stop_source());
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // give the server some time to start listening

    std::atomic<int> responseCount = 0;

    // Client that doesn't verify the server's certificate
    RestClient       doesntCare(VerifyServerCertificates(false));
    client::Command  req0;
    req0.command  = mdp::Command::Get;
    req0.topic           = URI<>(std::format("https://localhost:{}/sayhello?client=doesntCare", kServerPort));
    req0.clientRequestID = opencmw::IoBuffer("0");
    req0.callback = [&responseCount](const mdp::Message &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.protocolName == "https");
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == "Hello, World!");
        REQUIRE(msg.clientRequestID.asString() == "0");
        REQUIRE(msg.topic == URI<>("/sayhello"));
        responseCount++;
    };
    doesntCare.request(std::move(req0));

    // Client that verifies the server's certificate and trusts its CA
    RestClient      trusting(ClientCertificates(testServerCertificates.caCertificate));
    client::Command req1;
    req1.command         = mdp::Command::Get;
    req1.topic           = URI<>(std::format("https://localhost:{}/sayhello?client=trusting", kServerPort));
    req1.clientRequestID = opencmw::IoBuffer("0");
    req1.callback        = [&responseCount](const mdp::Message &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.protocolName == "https");
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == "Hello, World!");
        REQUIRE(msg.clientRequestID.asString() == "0");
        REQUIRE(msg.topic.path() == "/sayhello");
        responseCount++;
    };
    trusting.request(std::move(req1));

    // Client that verifies the server's certificate but doesn't trust its CA
    RestClient      notTrusting;
    client::Command req2;
    req2.command         = mdp::Command::Get;
    req2.topic           = URI<>(std::format("https://localhost:{}/sayhello?client=notTrusting", kServerPort));
    req2.clientRequestID = opencmw::IoBuffer("0");
    req2.callback        = [&responseCount](const mdp::Message &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.protocolName == "https");
        REQUIRE(msg.error.contains("Could not connect to endpoint"));
        REQUIRE(msg.clientRequestID.asString() == "0");
        REQUIRE(msg.topic.path() == "/sayhello");
        responseCount++;
    };
    notTrusting.request(std::move(req2));

    REQUIRE(waitFor(responseCount, 3));
}

TEST_CASE("GET/SET", "[http2]") {
    auto    serverThread = std::jthread([](std::stop_token stopToken) {
        RestServer server;
        REQUIRE(server.bind(kServerPort, majordomo::rest::Http2));

        std::deque<Message> messages;
        ensureMessageReceived(server, stopToken, messages);
        REQUIRE(messages.size() >= 1);
        const auto req0 = std::move(messages[0]);
        messages.pop_front();
        REQUIRE(req0.command == mdp::Command::Get);
        REQUIRE(normalize(req0.topic) == normalize(URI<>("/foo?contentType=application%2Fjson&param1=1&param2=foo%2Fbar")));
        REQUIRE(req0.error.empty());
        REQUIRE(req0.data.empty());

        Message reply0;
        reply0.command         = mdp::Command::Final;
        reply0.clientRequestID = req0.clientRequestID;
        reply0.topic           = URI<>("/foo?param1=1&param2=foo%2fbar");
        reply0.data            = opencmw::IoBuffer("Hello, World!");
        server.handleResponse(std::move(reply0));

        ensureMessageReceived(server, stopToken, messages);
        REQUIRE(messages.size() >= 1);
        const auto req1 = std::move(messages[0]);
        messages.pop_front();
        REQUIRE(req1.command == mdp::Command::Get);
        REQUIRE(normalize(req1.topic) == normalize(URI<>("/bar?contentType=application%2Fjson&param1=1&param2=2")));
        REQUIRE(req1.error.empty());
        REQUIRE(req1.data.empty());

        Message reply1;
        reply1.command         = mdp::Command::Final;
        reply1.clientRequestID = req1.clientRequestID;
        reply1.topic           = URI<>("/bar?param1=1&param2=2");
        reply1.error           = "'bar' not found";
        server.handleResponse(std::move(reply1));

        ensureMessageReceived(server, stopToken, messages);
        REQUIRE(messages.size() >= 1);
        const auto req2 = std::move(messages[0]);
        messages.pop_front();
        REQUIRE(req2.command == mdp::Command::Set);
        REQUIRE(normalize(req2.topic) == normalize(URI<>("/setexample?contentType=application%2Fjson")));
        REQUIRE(req2.error.empty());
        REQUIRE(req2.data.asString() == "Some data with\ttabs\nand newlines\x01");

        Message reply2;
        reply2.command         = mdp::Command::Final;
        reply2.clientRequestID = req2.clientRequestID;
        reply2.topic           = URI<>("/setexample");
        reply2.data            = opencmw::IoBuffer("value set");
        server.handleResponse(std::move(reply2));

        ensureMessageReceived(server, stopToken, messages); // makes sure responses are sent
    });

    Stopper stopper(serverThread.get_stop_source());
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // give the server some time to start listening

    std::atomic<int>         responseCount = 0;

    client::RestClient       client(client::VerifyServerCertificates(false));

    opencmw::client::Command req0;
    req0.command         = mdp::Command::Get;
    req0.clientRequestID = opencmw::IoBuffer("0");
    req0.topic           = URI<>(std::format("http://localhost:{}/foo?param1=1&param2=foo%2fbar", kServerPort));
    req0.callback        = [&responseCount](const mdp::Message &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.protocolName == "http");
        REQUIRE(msg.error == "");
        REQUIRE(msg.data.asString() == "Hello, World!");
        REQUIRE(msg.clientRequestID.asString() == "0");
        REQUIRE(msg.topic == URI<>("/foo?param1=1&param2=foo%2fbar"));
        responseCount++;
    };
    client.request(std::move(req0));

    client::Command req1;
    req1.command         = mdp::Command::Get;
    req1.clientRequestID = opencmw::IoBuffer("1");
    req1.topic           = URI<>(std::format("http://localhost:{}/bar?param1=1&param2=2", kServerPort));
    req1.callback        = [&responseCount](const mdp::Message &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.protocolName == "http");
        REQUIRE(msg.error == "'bar' not found");
        REQUIRE(msg.data.asString() == "");
        REQUIRE(msg.clientRequestID.asString() == "1");
        REQUIRE(msg.topic == URI<>("/bar?param1=1&param2=2"));
        responseCount++;
    };
    client.request(std::move(req1));

    client::Command req2;
    req2.command         = mdp::Command::Set;
    req2.clientRequestID = opencmw::IoBuffer("2");
    req2.topic           = URI<>(std::format("http://localhost:{}/setexample", kServerPort));
    req2.data            = opencmw::IoBuffer("Some data with\ttabs\nand newlines\x01");
    req2.callback        = [&responseCount](const mdp::Message &msg) {
        REQUIRE(msg.command == mdp::Command::Final);
        REQUIRE(msg.protocolName == "http");
        REQUIRE(msg.clientRequestID.asString() == "2");
        REQUIRE(msg.topic == URI<>("/setexample"));
        REQUIRE(msg.data.asString() == "value set");
        responseCount++;
    };
    client.request(std::move(req2));

    waitFor(responseCount, 3);
}

TEST_CASE("Long polling example", "[http2]") {
    constexpr int kFooMessages = 50;

    auto          brokerThread = std::jthread([](std::stop_token stopToken) {
        RestServer server;
        REQUIRE(server.bind(kServerPort, majordomo::rest::Http2));

        const auto                  topic = URI<>("/foo?param1=1&param2=foo%2Fbar");

        std::deque<Message>         messages;
        ensureMessageReceived(server, stopToken, messages);
        REQUIRE(messages.size() >= 1);
        const auto req0 = std::move(messages[0]);
        messages.pop_front();
        REQUIRE(req0.command == mdp::Command::Subscribe);
        REQUIRE(normalize(req0.topic) == normalize(topic));
        REQUIRE(req0.error.empty());
        REQUIRE(req0.data.empty());

        for (int i = 0; i < kFooMessages; ++i) {
            Message notify;
            notify.command     = mdp::Command::Notify;
            notify.serviceName = "/foo";
            notify.topic       = topic;
            auto data          = std::to_string(i);
            notify.data        = opencmw::IoBuffer(data.data(), data.size());
            server.handleNotification(opencmw::mdp::Topic::fromMdpTopic(topic), std::move(notify));
        }

        ensureMessageReceived(server, stopToken, messages, std::chrono::seconds(10));

        // sync with the client to ensure the client unsubscribed before we send more notifications
        REQUIRE(messages.size() >= 1);
        const auto req1 = std::move(messages[0]);
        messages.pop_front();
        REQUIRE(req1.command == mdp::Command::Get);
        Message reply1;
        reply1.command         = mdp::Command::Final;
        reply1.clientRequestID = req1.clientRequestID;
        reply1.topic           = topic;
        reply1.data            = opencmw::IoBuffer("Hello");
        server.handleResponse(std::move(reply1));

        // send notifications that should not be received
        for (int i = 0; i < kFooMessages; ++i) {
            Message notify;
            notify.command     = mdp::Command::Notify;
            notify.serviceName = "/foo";
            notify.topic       = topic;
            auto data          = std::to_string(kFooMessages + i);
            notify.data        = opencmw::IoBuffer(data.data(), data.size());
            server.handleNotification(opencmw::mdp::Topic::fromMdpTopic(topic), std::move(notify));
        }

        ensureMessageReceived(server, stopToken, messages, std::chrono::seconds(10)); // makes sure messages are processed
    });

    Stopper       stopper(brokerThread.get_stop_source());
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // give the server some time to start listening

    {
        std::atomic<int>         responseCount = 0;

        client::RestClient       client;
        opencmw::client::Command sub;
        sub.command  = mdp::Command::Subscribe;
        sub.clientRequestID = opencmw::IoBuffer("0");
        sub.topic           = URI<>(std::format("http://localhost:{}/foo?param1=1&param2=foo%2fbar", kServerPort));
        sub.callback = [&responseCount](const mdp::Message &msg) {
            REQUIRE(msg.command == mdp::Command::Notify);
            REQUIRE(msg.error == "");
            REQUIRE(msg.data.asString() == std::to_string(responseCount));
            REQUIRE(msg.protocolName == "http");
            REQUIRE(normalize(msg.topic) == normalize(URI<>("/foo?param1=1&param2=foo%2Fbar")));
            responseCount++;
        };
        client.request(std::move(sub));
        waitFor(responseCount, kFooMessages);
        responseCount = 0;

        // unsubscribe
        client::Command unsub;
        unsub.command         = mdp::Command::Unsubscribe;
        unsub.clientRequestID = opencmw::IoBuffer("0");
        unsub.topic           = URI<>(std::format("http://localhost:{}/foo?param1=1&param2=foo%2fbar", kServerPort));
        unsub.callback        = [](const mdp::Message &) {
            FAIL("Unsubscribe should not receive a message");
        };
        client.request(std::move(unsub));

        // Send a GET to sync with server
        client::Command get;
        get.command         = mdp::Command::Get;
        get.clientRequestID = opencmw::IoBuffer("1");
        get.topic           = URI<>(std::format("http://localhost:{}/foo?param1=1&param2=foo%2fbar", kServerPort));
        get.callback        = [&responseCount](const mdp::Message &msg) {
            REQUIRE(msg.command == mdp::Command::Final);
            REQUIRE(msg.protocolName == "http");
            REQUIRE(msg.error == "");
            REQUIRE(msg.data.asString() == "Hello");
            REQUIRE(normalize(msg.topic) == normalize(URI<>("/foo?param1=1&param2=foo%2Fbar")));
            responseCount++;
        };
        client.request(std::move(get));

        waitFor(responseCount, 1);
        responseCount -= 1;

        // wait some time to make sure no more messages are received
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        REQUIRE(responseCount == 0);
    }
}
