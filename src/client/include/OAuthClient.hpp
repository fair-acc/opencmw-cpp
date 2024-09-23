#ifndef OPENCMW_CPP_OAUTHCLIENT_HPP
#define OPENCMW_CPP_OAUTHCLIENT_HPP

#include "IoBuffer.hpp"
#include "IoSerialiserJson.hpp"
#include "URI.hpp"
#include <httplib.h>

namespace opencmw {

class Token {
public:
    std::string                                                       _token;
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> _expiry; // time when this token will expire if any

    explicit Token(const std::string &token = "")
        : _token(token) {}
    explicit Token(const std::string &token, std::chrono::seconds expiresIn)
        : Token(token) {
        // convenience constructor for OAuth access token responses (expires_in is always in seconds)
        _expiry = std::chrono::steady_clock::now() + expiresIn;
    }

    bool expired() const {
        return _expiry && *_expiry < std::chrono::steady_clock::now();
    }

    bool valid() const {
        return !_token.empty() && !expired();
    }
};

struct OAuthResponse {
    int         status = -1;
    std::string body;
};

// response as per https://datatracker.ietf.org/doc/html/rfc6749#section-5.1
struct AccessTokenResponse {
    std::string access_token;
    std::string token_type;
    int         expires_in = 3600;
    std::string refresh_token;
    std::string bullshit;
};
} // namespace opencmw

ENABLE_REFLECTION_FOR(opencmw::AccessTokenResponse, access_token, token_type, expires_in, refresh_token, bullshit)

namespace opencmw {

// This class implements an RFC 6749 compliant OAuth client
// https://datatracker.ietf.org/doc/html/rfc6749
class OAuthClient {
private:
    using StrictUri                                  = opencmw::URI<opencmw::uri_check::STRICT>;
    std::string                            _scope    = "";
    std::string                            _clientId = "";
    StrictUri                              _redirectUri;
    StrictUri                              _endpoint;
    StrictUri                              _tokenEndpoint;
    std::string                            _clientSecret; // only needed for confidential access type / client authentication
    httplib::Server                        _srv;
    std::unique_ptr<std::thread>           _thread;
    std::string                            _authCode;
    std::function<bool(const StrictUri &)> _openAuthorizationUriCallback = &OAuthClient::openWebBrowser; // open Uri in web browser by default
    Token                                  _accessToken;
    Token                                  _refreshToken;

public:
    explicit OAuthClient(const std::string &scope, const std::string &clientId, StrictUri redirectUri, StrictUri endpoint, StrictUri tokenEndpoint, const std::string &clientSecret = {})
        : _scope(scope), _clientId(clientId), _redirectUri(redirectUri), _endpoint(endpoint), _tokenEndpoint(tokenEndpoint), _clientSecret(clientSecret) {
        _srv.Get("/", [&](const httplib::Request req, httplib::Response &res) {
            // response as per https://datatracker.ietf.org/doc/html/rfc6749#section-4.1.2
            for (const auto &[k, v] : req.params) {
                if (k == "code") {
                    _authCode = v;
                }
            }
            if (_authCode.empty()) {
                res.set_content("Did not receive an RFC 6749-compliant response.", "text/plain");
                res.status = 401; // Unauthorized
            } else {
                res.set_content("Authorization complete. You can close this browser window now.\n", "text/plain");
            }
            requestToken();
            _srv.stop();
        });
    }

    static bool openWebBrowser(const StrictUri &uri) {
        fmt::println("Opening {} in a web browser...", uri.str());
#ifdef __unix__
        // An alternative would be to use the org.freedesktop.portal.OpenURI method, but that would require dbus
        // and it is more likely that xdg-open is present, than the portal running
        return !std::system(fmt::format("xdg-open '{}'", uri.str()).c_str());
#else
        std::cout << "Your platform is unsupported, please open the link manually." << std::endl;
        return false;
#endif
    }

    void setAuthorizationUriCallback(decltype(_openAuthorizationUriCallback) callback) {
        _openAuthorizationUriCallback = callback;
    }

    bool requestAuthorization() {
        const auto hostname = _redirectUri.hostName();
        const auto port     = _redirectUri.port();
        if (!hostname || !port) {
            return false;
        }
        _thread = std::make_unique<std::thread>([this, hostname, port]() {
            _srv.listen(*hostname, *port);
        });

        // parameters as per https://datatracker.ietf.org/doc/html/rfc6749#section-4.1.1
        std::unordered_map<std::string, std::optional<std::string>> params{ { "scope", _scope }, { "response_type", "code" }, { "client_id", _clientId }, { "redirect_uri", _redirectUri.str() } };
        if (!_clientSecret.empty()) {
            params.emplace("client_secret", _clientSecret);
        }
        auto uri = StrictUri::UriFactory(_endpoint).setQuery(params).build();
        return _openAuthorizationUriCallback(uri);
    }

    static std::optional<httplib::Client> getClient(const StrictUri &endpoint) {
        const auto scheme   = endpoint.scheme();
        const auto hostname = endpoint.hostName();
        const auto port     = endpoint.port();
        const auto path     = endpoint.path();
        if (!scheme || !hostname || !port || !path || path->empty()) {
            return std::nullopt;
        }

        return httplib::Client(*scheme + "://" + *hostname + ":" + std::to_string(*port));
    }

    bool getAccessToken(const httplib::Params &params) {
        auto client = getClient(_tokenEndpoint);
        if (!client) {
            return false;
        }

        auto res = client->Post(*_tokenEndpoint.path(), params);
        if (!res || res->status != 200) {
            return false;
        }
        opencmw::IoBuffer   buf{ res->body.c_str() };
        AccessTokenResponse resp;
        opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buf, resp);
        if (resp.access_token.empty()) {
            return false;
        }

        _accessToken  = Token(resp.access_token, std::chrono::seconds(resp.expires_in));
        _refreshToken = Token(resp.refresh_token);
        return true;
    }

    bool requestToken() {
        if (_authCode.empty()) {
            return false;
        }

        // parameters as per https://datatracker.ietf.org/doc/html/rfc6749#section-4.1.3
        return getAccessToken({ { "grant_type", "authorization_code" }, { "code", _authCode }, { "redirect_uri", _redirectUri.str() }, { "client_id", _clientId } });
    }

    bool refreshAccessToken() {
        if (!_refreshToken.valid()) {
            return false;
        }
        // parameters as per https://datatracker.ietf.org/doc/html/rfc6749#section-6
        return getAccessToken({ { "grant_type", "refresh_token" }, { "refresh_token", _refreshToken._token }, { "client_id", _clientId } });
    }

    OAuthResponse get(const StrictUri uri) {
        auto client = getClient(uri);
        if (!client) {
            return OAuthResponse();
        }

        auto          res = client->Get(*uri.path(), httplib::Headers({ { "Authorization", "Bearer " + _accessToken._token } }));
        OAuthResponse result;
        result.status = res->status;
        result.body   = res->body;
        return result;
    }

    void wait() {
        if (_thread->joinable()) {
            _thread->join();
        }
    }

    bool ready() {
        return _accessToken.valid();
    }
};

} // namespace opencmw

#endif // OPENCMW_CPP_OAUTHCLIENT_HPP
