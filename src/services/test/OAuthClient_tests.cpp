#define CATCH_CONFIG_MAIN // This tells the catch header to generate a main

#include <catch2/catch.hpp>

#ifndef __EMSCRIPTEN__

#include <Client.hpp>
#include <concepts/majordomo/helpers.hpp>
#include <majordomo/Rbac.hpp>
#include <services/OAuthClient.hpp>

namespace opencmw_oauth_client_test {

// Usually you want to open an external browser for the authorization flow.
// But this instead simulates a browser with a httplib client,
// so that the test can be run headless in CI
bool loginAtUri(const opencmw::URI<opencmw::STRICT> &uri) {
    std::cout << "Simulating to login with a browser at " << uri.str() << std::endl;

    auto browser     = opencmw::OAuthClient::getClient(uri);
    auto relativeRef = uri.relativeRef();
    REQUIRE(browser);
    REQUIRE(relativeRef);

    // We have to include the params in the request path
    httplib::Result res = browser->Get(*relativeRef);
    REQUIRE(res);
    // Now the cursed part begins, first find the HTML submit URL of the credentials form
    const auto begin = res->body.find("action=\"");
    REQUIRE(begin != std::string::npos);
    // search for the enclosing ", luckily every " inside the URI is URI-encoded
    const auto end = res->body.find("\"", begin + 8);
    REQUIRE(end != std::string::npos);
    auto action = res->body.substr(begin + 8, end - begin - 8);

    // unescape every HTML escaped &
    std::size_t n = 0;
    while ((n = action.find("&amp;", n)) != std::string::npos) {
        action.replace(n, 5, "&");
    }

    const opencmw::URI actionUri{ action };

    // Of course we also have to simulate a cookie store, because otherwise Keycloak denies access
    std::string cookies;
    for (const auto &[k, v] : res->headers) {
        if (k == "Set-Cookie") {
            // to make parsing easier, remove all cookie metadata and just always send all cookies
            const auto semicolon = v.find(';');
            cookies              = cookies + (semicolon == std::string::npos ? v : v.substr(0, semicolon)) + "; ";
        }
    }
    REQUIRE(cookies.size() > 2);
    // According to https://datatracker.ietf.org/doc/html/rfc6265#section-4.1 cookies must not have a trailing semicolon
    cookies.resize(cookies.size() - 2);

    // Cookies ready, now hit the endpoint
    httplib::Headers headers{ { { "Cookie", cookies } } };
    relativeRef = actionUri.relativeRef();
    REQUIRE(relativeRef);
    httplib::Params params{ { { "username", "testuser" }, { "password", "testuser" }, { "credentialId", "" } } };

    res = browser->Post(*relativeRef, headers, params);
    REQUIRE(res);
    REQUIRE(res->status == 302);
    headers = res->headers;

    // follow the redirect
    const auto it = headers.find("Location");
    REQUIRE(it != headers.cend());
    const opencmw::URI redirecUri{ it->second };
    auto               redirectClient = opencmw::OAuthClient::getClient(redirecUri);
    relativeRef                       = redirecUri.relativeRef();
    REQUIRE(relativeRef);
#if 0
    // TODO: Why does this crash?
    // In theory we should be able to use httplib to call the redirect URI
    res = redirectClient->Get(*relativeRef);
    REQUIRE(res);
#else
    // call the final redirect URI with curl as a workaround
    const std::string curl = "curl '" + it->second + "'";
    // flawfinder: ignore
    res->status            = std::system(curl.c_str()) + 200;
#endif
    REQUIRE(res->status == 200);

    return true;
}

TEST_CASE("Worker test", "[OAuth]") {
    opencmw::majordomo::Broker<> broker{ "/Broker", {} };
    opencmw::OAuthWorker         oauthWorker{ opencmw::URI("http://localhost:8091"), opencmw::URI("http://localhost:8090/realms/testrealm/protocol/openid-connect/auth"), opencmw::URI("http://localhost:8090/realms/testrealm/protocol/openid-connect/token"), broker };

    REQUIRE(broker.bind(opencmw::URI<>("mds://127.0.0.1:12345")));
    REQUIRE(broker.bind(opencmw::URI<>("mdp://127.0.0.1:12346")));

    RunInThread brokerThread(broker);
    RunInThread oauthThread(oauthWorker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, oauthWorker));

    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(broker.context, 20ms, ""));
    opencmw::client::ClientContext client{ std::move(clients) };

    opencmw::IoBuffer              inBuf;
    const std::string              scope{ "openid" };
    opencmw::OAuthInput            in{ scope, "testclientid" };
    const auto [pub, priv] = opencmw::majordomo::cryptography::generateKeyPair();
    in.publicKey           = std::string(pub.key, pub.key + crypto_sign_PUBLICKEYBYTES);
    opencmw::serialise<opencmw::YaS>(inBuf, in);

    std::condition_variable cv;
    std::mutex              m;
    bool                    done = false;
    std::string             expectedHash;

    client.set(opencmw::URI("mdp://127.0.0.1:12346/oauth"), [&](const mdp::Message &reply) {
        opencmw::OAuthOutput out;
        auto outBuf = reply.data;
        opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(outBuf, out);

        REQUIRE(out.authorizationUri.size());
        REQUIRE(out.secret.size());
        REQUIRE(loginAtUri(opencmw::URI(out.authorizationUri)));

        opencmw::IoBuffer iBuf;
        opencmw::OAuthInput codeIn{ scope, "testclientid", "", out.secret };
        opencmw::serialise<opencmw::YaS>(iBuf, codeIn);
        client.set(opencmw::URI("mdp://127.0.0.1:12346/oauth"), [&](const mdp::Message& rep) {
                opencmw::OAuthOutput tokenOut;
                auto tokenOutBuf = rep.data;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(tokenOutBuf, tokenOut);

                // we must have got an access token now
                const auto accessToken = tokenOut.accessToken;
                REQUIRE(accessToken.size());

                // the public key storage should have our key now
                opencmw::IoBuffer keyBuf;
                expectedHash = opencmw::KeystoreWorker::keyHash(in.publicKey);
                opencmw::KeystoreInput keyIn{ expectedHash };
                opencmw::serialise<opencmw::YaS>(keyBuf, keyIn);
                client.set(opencmw::URI("mdp://127.0.0.1:12346/keystore"), [&](const mdp::Message &keyResp) {
                        opencmw::KeystoreOutput keyOut;
                        auto keyOutBuf = keyResp.data;
                        opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(keyOutBuf, keyOut);

                        // the hashes should be the same
                        REQUIRE(keyOut.key.size());
                        const auto returnedHash = opencmw::KeystoreWorker::keyHash(keyOut.key);
                        REQUIRE(expectedHash == returnedHash);
                        // the key is associated with the correct role
                        REQUIRE(keyOut.role == scope);
                        REQUIRE(keyOut.expiryDate > std::chrono::system_clock::now().time_since_epoch().count());
                        std::lock_guard lk(m);
                        done = true;
                        cv.notify_one(); }, std::move(keyBuf));
        }, std::move(iBuf)); }, std::move(inBuf));

    std::unique_lock lk(m);
    cv.wait(lk, [&] { return done; });
    oauthWorker.stop();
}

} // namespace opencmw_oauth_client_test
#endif
