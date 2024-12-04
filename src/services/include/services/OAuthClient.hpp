#ifndef OPENCMW_CPP_OAUTHCLIENT_HPP
#define OPENCMW_CPP_OAUTHCLIENT_HPP

#include "IoBuffer.hpp"
#include "IoSerialiserJson.hpp"
#include "KeyStore.hpp"
#include "URI.hpp"
#include <httplib.h>
#include <majordomo/Worker.hpp>

namespace opencmw {

struct OAuthContext {
    std::string secretToken;
};

struct OAuthInput {
    std::string scope;
    std::string clientId;
    std::string clientSecret;
    std::string secret;
    std::string publicKey;
};

struct OAuthOutput {
    std::string authorizationUri;
    std::string secret;
    std::string accessToken;
    std::string refreshToken;
};
} // namespace opencmw

ENABLE_REFLECTION_FOR(opencmw::OAuthContext, secretToken)
ENABLE_REFLECTION_FOR(opencmw::OAuthInput, scope, clientId, clientSecret, secret, publicKey)
ENABLE_REFLECTION_FOR(opencmw::OAuthOutput, authorizationUri, secret, accessToken, refreshToken)

namespace opencmw {

class Token {
public:
    std::string                                                       _token;
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> _expiry; // time when this token will expire if any

    Token(const std::string &token = "")
        : _token(token) {}
    Token(const std::string &token, std::chrono::seconds expiresIn)
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

struct OAuthAccess {
    Token accessToken;
    Token refreshToken;
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

using StrictUri = opencmw::URI<opencmw::uri_check::STRICT>;

// This class implements an RFC 6749 compliant OAuth client
// https://datatracker.ietf.org/doc/html/rfc6749
class OAuthClient {
private:
    StrictUri                              _redirectUri;
    StrictUri                              _endpoint;
    StrictUri                                                              _tokenEndpoint;
    httplib::Server                        _srv;
    std::unique_ptr<std::thread>                                           _thread;
    std::function<void(const std::string &code, const std::string &state)> _endpointCallback;

public:
    explicit OAuthClient(StrictUri redirectUri, StrictUri endpoint, StrictUri tokenEndpoint)
        : _redirectUri(redirectUri), _endpoint(endpoint), _tokenEndpoint(tokenEndpoint) {
        _srv.Get("/", [&](const httplib::Request req, httplib::Response &res) {
            std::string code, state;
            // response as per https://datatracker.ietf.org/doc/html/rfc6749#section-4.1.2
            for (const auto &[k, v] : req.params) {
                if (k == "code") {
                    code = v;
                } else if (k == "state") {
                    state = v;
                }
            }
            if (code.empty()) {
                res.set_content("Did not receive an RFC 6749-compliant response.", "text/plain");
                res.status = 401; // Unauthorized
            } else {
                res.set_content("Authorization complete. You can close this browser window now.\n", "text/plain");
                if (_endpointCallback) {
                    _endpointCallback(code, state);
                }
            }
        });
        _thread = std::make_unique<std::thread>([this]() {
            _srv.listen(*_redirectUri.hostName(), *_redirectUri.port());
        });
    }

    // convenience API function for clients who just want to open the URI in a web browser
    static bool openWebBrowser(const StrictUri &uri) {
        fmt::println("Opening {} in a web browser...", uri.str());
#ifdef __unix__
        // An alternative would be to use the org.freedesktop.portal.OpenURI method, but that would require dbus
        // and it is more likely that xdg-open is present, than the portal running
        // flawfinder: ignore
        return !std::system(fmt::format("xdg-open '{}'", uri.str()).c_str());
#else
        std::cout << "Your platform is unsupported, please open the link manually." << std::endl;
        return false;
#endif
    }

    void setEndpointCallback(decltype(_endpointCallback) callback) {
        _endpointCallback = callback;
    }

    StrictUri authorizationUri(const std::string &scope, const std::string &clientId, const std::string &state = "", const std::string &clientSecret = "") const {
        // parameters as per https://datatracker.ietf.org/doc/html/rfc6749#section-4.1.1
        std::unordered_map<std::string, std::optional<std::string>> params{ { "scope", scope }, { "response_type", "code" }, { "client_id", clientId }, { "redirect_uri", _redirectUri.str() } };

        if (!state.empty()) {
            params.emplace("state", state);
        }

        if (!clientSecret.empty()) {
            params.emplace("client_secret", clientSecret);
        }

        return StrictUri::UriFactory(_endpoint).setQuery(params).build();
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

    OAuthAccess getAccessToken(const httplib::Params &params) {
        auto client = getClient(_tokenEndpoint);
        if (!client) {
            return {};
        }

        auto res = client->Post(*_tokenEndpoint.path(), params);
        if (!res || res->status != 200) {
            return {};
        }
        opencmw::IoBuffer   buf{ res->body.c_str() };
        AccessTokenResponse resp;
        opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buf, resp);
        if (resp.access_token.empty()) {
            return {};
        }

        return { Token(resp.access_token, std::chrono::seconds(resp.expires_in)), Token(resp.refresh_token) };
    }

    OAuthAccess requestToken(const std::string &authCode, const std::string &clientId) {
        if (authCode.empty() || clientId.empty()) {
            return {};
        }

        // parameters as per https://datatracker.ietf.org/doc/html/rfc6749#section-4.1.3
        return getAccessToken({ { "grant_type", "authorization_code" }, { "code", authCode }, { "redirect_uri", _redirectUri.str() }, { "client_id", clientId } });
    }

    OAuthAccess refreshAccessToken(const std::string &refreshToken, const std::string &clientId) {
        if (refreshToken.empty()) {
            return {};
        }
        // parameters as per https://datatracker.ietf.org/doc/html/rfc6749#section-6
        return getAccessToken({ { "grant_type", "refresh_token" }, { "refresh_token", refreshToken }, { "client_id", clientId } });
    }

    void stop() {
        _srv.stop();
        _thread->join();
    }
};

template<typename T>
T makeRandom(uint32_t size) {
    std::random_device               rd;
    std::mt19937                     generator(rd());
    std::uniform_int_distribution<T> distribution(0, static_cast<T>(size));

    return distribution(generator);
}

template<>
inline std::string makeRandom(uint32_t size) {
    std::string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    std::string randomString(size, ' ');
    std::generate(randomString.begin(), randomString.end(), [&]() {
        return characters[makeRandom<uint32_t>(static_cast<uint32_t>(characters.length() - 1))];
    });

    return randomString;
}

namespace {
using OAuthWorkerType = majordomo::Worker<"/oauth", OAuthContext, OAuthInput, OAuthOutput, majordomo::description<"Authorization with OAuth2">>;
};

class OAuthWorker : public OAuthWorkerType {
public:
    explicit OAuthWorker(StrictUri redirectUri, StrictUri endpoint, StrictUri tokenEndpoint, StrictUri brokerAddress, const zmq::Context &context, majordomo::Settings settings = {})
        : OAuthWorkerType(brokerAddress, {}, context, settings), _client(redirectUri, endpoint, tokenEndpoint), _keystore(brokerAddress, context, settings) { init(); };
    template<typename BrokerType>
    explicit OAuthWorker(StrictUri redirecturi, StrictUri endpoint, StrictUri tokenEndpoint, const BrokerType &broker)
        : OAuthWorkerType(broker, {}), _client(redirecturi, endpoint, tokenEndpoint), _keystore(broker) { init(); };
    ~OAuthWorker() {
        _keystore.shutdown();
        _keystoreThread.join();
    }

    void stop() {
        _client.stop();
    }

private:
    std::map<std::string, std::string> _secrets;
    std::map<std::string, RoleKey>     _pendingKeys;
    OAuthClient                        _client;
    KeystoreWorker                     _keystore;
    std::thread                        _keystoreThread;
    void                               init() {
        _client.setEndpointCallback([this](const std::string &code, const std::string &state) {
            _secrets[state] = code;
            if (_pendingKeys.contains(state)) {
                // add the key to the public key storage
                const auto key = _pendingKeys.at(state);
                _pendingKeys.erase(state);
                _keystore.addKey(key);
            }
        });
        OAuthWorkerType::setCallback([this](const majordomo::RequestContext &rawCtx, const OAuthContext &context, const OAuthInput &in, OAuthContext &replyContext, OAuthOutput &out) {
            if (in.secret.empty() && !in.publicKey.empty()) {
                // first stage, client gets a secret in the response back plus an URI to authorize at
                constexpr std::size_t secretLength = 64;
                const auto            secret       = makeRandom<std::string>(secretLength);
                // keys expire in 1 year by default
                const auto expiresAt               = std::chrono::system_clock::now() + std::chrono::years(1);
                _pendingKeys[secret]               = { in.scope, in.publicKey, expiresAt.time_since_epoch().count() };
                out.authorizationUri               = _client.authorizationUri(in.scope, in.clientId, secret).str();
                out.secret                         = secret;
            } else if (_secrets.contains(in.secret)) {
                // second stage, client has authorized and can get an access token
                const auto code   = _secrets.at(in.secret);
                const auto access = _client.requestToken(code, in.clientId);
                out.accessToken   = access.accessToken._token;
                out.refreshToken  = access.refreshToken._token;
            }
        });
        _keystoreThread = std::thread([this] { _keystore.run(); });
    }
};

} // namespace opencmw

#endif // OPENCMW_CPP_OAUTHCLIENT_HPP
