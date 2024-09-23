#include <catch2/catch.hpp>

#include <OAuthClient.hpp>

namespace opencmw_oauth_client_test {
TEST_CASE("Authorization Code Grant test", "[OAuth]") {
    // Usually you want to open an external browser for the authorization flow.
    // But this instead simulates a browser with a httplib client,
    // so that the test can be run headless in CI
    constexpr bool       mustUseExternalBrowser = false;

    opencmw::OAuthClient client{ "openid", "testclientid", opencmw::URI("http://localhost:8081"), opencmw::URI("http://localhost:8080/realms/testrealm/protocol/openid-connect/auth"), opencmw::URI("http://localhost:8080/realms/testrealm/protocol/openid-connect/token") };

    if constexpr (!mustUseExternalBrowser) {
        const auto callback = [&](const auto &uri) {
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
            res->status            = std::system(curl.c_str()) + 200;
#endif
            REQUIRE(res->status == 200);

            return true;
        };
        client.setAuthorizationUriCallback(callback);
    }

    REQUIRE(client.requestAuthorization());
    client.wait();
    // got an access token
    REQUIRE(client.ready());
    // can refresh the access token
    REQUIRE(client.refreshAccessToken());
    const auto resp = client.get(opencmw::URI("http://localhost:8080/realms/testrealm/protocol/openid-connect/userinfo"));
    REQUIRE(resp.status == 200);
}
} // namespace opencmw_oauth_client_test
