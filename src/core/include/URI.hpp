#ifndef OPENCMW_CPP_URI_HPP
#define OPENCMW_CPP_URI_HPP
#include <algorithm>
#include <fmt/format.h>
#include <iomanip>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include <cassert>

namespace opencmw {

struct URISyntaxException : public std::ios_base::failure {
    explicit URISyntaxException()
        : std::ios_base::failure("unknown URI exception") {}
    explicit URISyntaxException(const std::string &errorMsg)
        : std::ios_base::failure(errorMsg) {}
    explicit URISyntaxException(const char *errorMsg)
        : std::ios_base::failure(errorMsg) {}
};

enum uri_check {
    STRICT, // checks for RFC-3986-restricted ascii-characters only and throws exception if violated
    RELAXED // checks for RFC-3986-restricted only
};

// parse RFC 3986 URIs of the form [scheme:][//authority][/path][?query parameter][#fragment]
// authority can be [<user>[:password]@]<host>[:port]
// simple parser for internal use (N.B. does handle all nominal but not necessarily all corner-failure-cases)
// compiler-explorer test: https://compiler-explorer.com/z/a7W3eosYM
// regex test: regexr.com/69hv8
template<uri_check check = STRICT>
class URI {
    std::string _str; // raw URI string
    // need to keep a local, owning, and immutable copy of the source template
    // evaluate on demand and if available
    std::string_view         _scheme;
    std::string_view         _authority;
    mutable bool             _parsedAuthority = false;
    mutable std::string_view _userName;
    mutable std::string_view _pwd;
    mutable std::string_view _hostName;
    mutable std::string_view _port;
    std::string_view         _path;
    std::string_view         _query;
    std::string_view         _fragment;

    // computed on-demand
    mutable std::unordered_map<std::string, std::optional<std::string>> _queryMap;

public:
    URI() = delete;
    explicit URI(const std::string &src)
        : _str(src) { parseURI(); }
    explicit URI(std::string &&src)
        : _str(std::move(src)) { parseURI(); }
    URI(const URI &other) noexcept { *this = other; }
    URI(const URI &&other) noexcept { *this = std::move(other); }
    ~URI() = default;

    URI &operator=(const URI &other) noexcept {
        if (this == &other) {
            return *this;
        }
        _str              = other.str();
        auto adjustedView = [this, &other](std::string_view otherView) {
            return std::string_view(_str.data() + std::distance(other.str().data(), otherView.data()), otherView.size());
        };
        _scheme    = adjustedView(other._scheme);
        _authority = adjustedView(other._authority);
        _path      = adjustedView(other._path);
        _query     = adjustedView(other._query);
        _fragment  = adjustedView(other._fragment);
        if (_query.empty()) {
            _queryMap.clear();
        } else {
            _queryMap = other._queryMap;
        }

        _parsedAuthority = other._parsedAuthority;
        if (!_parsedAuthority) {
            return *this;
        }
        _userName = adjustedView(other._userName);
        _pwd      = adjustedView(other._pwd);
        _hostName = adjustedView(other._hostName);
        _port     = adjustedView(other._port);
        return *this;
    }

    URI &operator=(URI &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        const char *oldBegin = other._str.data();

        std::swap(_str, other._str);
        std::swap(_scheme, other._scheme);
        std::swap(_authority, other._authority);
        std::swap(_path, other._path);
        std::swap(_query, other._query);
        std::swap(_fragment, other._fragment);
        std::swap(_queryMap, other._queryMap);
        std::swap(_parsedAuthority, other._parsedAuthority);
        std::swap(_userName, other._userName);
        std::swap(_pwd, other._pwd);
        std::swap(_hostName, other._hostName);
        std::swap(_port, other._port);

        // small strings are copied, then adjust views
        if (_str.data() != oldBegin) {
            auto adjustView = [this, oldBegin](std::string_view &view) {
                view = std::string_view(_str.data() + std::distance(oldBegin, view.data()), view.size());
            };

            adjustView(_scheme);
            adjustView(_authority);
            adjustView(_path);
            adjustView(_query);
            adjustView(_fragment);
            adjustView(_userName);
            adjustView(_pwd);
            adjustView(_hostName);
            adjustView(_port);
        }

        return *this;
    }

    const std::string &str() const noexcept { return _str; }
    const std::string &operator()() const noexcept { return _str; }
    bool               empty() const { return _str.empty(); }

    static std::string encode(const std::string_view &source) noexcept {
        std::ostringstream encoded;
        encoded.fill('0');
        encoded << std::hex;

        for (auto c : source) {
            if (isUnreserved(c)) {
                // keep RFC 3986 section 2.3 Unreserved Characters i.e. [a-zA-Z0-9-_-~]
                encoded << c;
            } else {
                // percent-encode RFC 3986 section 2.2 Reserved Characters i.e. [!#$%&'()*+,/:;=?@[]]
                encoded << std::uppercase;
                encoded << '%' << std::setw(2) << int(static_cast<uint8_t>(c));
                encoded << std::nouppercase;
            }
        }

        return encoded.str();
    }

    static std::string decode(const std::string_view &s) {
        std::ostringstream decoded;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '%') {
                // percent-encode RFC 3986 section 2.2 Reserved Characters i.e. [!#$%&'()*+,/:;=?@[]]
                if constexpr (check == STRICT) {
                    if (!std::isxdigit(s[i + 1])) {
                        throw URISyntaxException(fmt::format("additional erroneous character '{}' found after '%' at {} of '{}'", s[i + 1], i + 1, s));
                    }
                }
                int                d;
                std::istringstream iss(std::string(s.substr(i + 1, 2))); // TODO find smarter conversion
                iss >> std::hex >> d;
                decoded << static_cast<uint8_t>(d);
                i += 2;
            } else {
                decoded << c;
            }
        }
        return decoded.str();
    }

    // clang-format off
    inline std::optional<std::string> scheme() const noexcept { return returnOpt(_scheme); }
    inline std::optional<std::string> authority() const noexcept { return returnOpt(_authority); }
    inline std::optional<std::string> user() const noexcept { parseAuthority(); return returnOpt(_userName); }
    inline std::optional<std::string> password() const noexcept { parseAuthority(); return returnOpt(_pwd); }
    inline std::optional<std::string> hostName() const noexcept { parseAuthority(); return returnOpt(_hostName); }
    inline std::optional<uint16_t> port() const noexcept { parseAuthority(); return _port.empty() ? std::nullopt : std::optional(std::stoi(std::string{ _port.begin(), _port.end() }));}
    inline std::optional<std::string> path() const noexcept { return returnOpt(_path); }
    inline std::optional<std::string> queryParam() const noexcept { return returnOpt(_query); }
    inline std::optional<std::string> fragment() const noexcept { return returnOpt(_fragment); }
    inline std::optional<std::string> relativeRef() const noexcept { // path + query + fragment
        using namespace fmt::literals;
        if (_path.empty() && _query.empty() && _fragment.empty()) return {};
        return fmt::format("{opt_path_slash}{path}{qMark}{query}{hashMark}{fragment}",
                           fmt::arg("opt_path_slash", _path.empty() || _path.starts_with('/') ? "" : "/"),
                           fmt::arg("path", _path),                     // path
                           fmt::arg("qMark", (_query.empty() || _query.starts_with('?')) ? "" : "?"),
                           fmt::arg("query", _query),                         // query
                           fmt::arg("hashMark", (_fragment.empty() || _fragment.starts_with('#')) ? "" : "#"),
                           fmt::arg("fragment", encode(_fragment))); // fragment
    }
    inline std::optional<std::string> relativeRefNoFragment() const noexcept { // path + query
        using namespace fmt::literals;
        if (_path.empty() && _query.empty()) return {};
        return fmt::format("{opt_path_slash}{path}{qMark}{query}",
                           fmt::arg("opt_path_slash", (_path.empty() || _path.starts_with('/')) ? "" : "/"), fmt::arg("path", _path),                     // path
                           fmt::arg("qMark", (_query.empty() || _query.starts_with('?')) ? "" : "?"), fmt::arg("query", _query));                        // query
    }
    // clang-format om

    // decompose map
    inline const std::unordered_map<std::string, std::optional<std::string>> &queryParamMap() const {
        if (_query.empty() || !_queryMap.empty()) { // empty query parameter or already parsed
            return _queryMap;
        }
        if constexpr (check == STRICT) {
            if (!std::all_of(_query.begin(), _query.end(), [](char c) { return isUnreserved(c) || c == '&' || c == ';' || c == '=' || c == '%' || c == ':' || c == '/'; })) {
                throw URISyntaxException(fmt::format("URI query contains illegal characters: {}", _query));
            }
        }
        size_t readPos = 0;
        while (readPos < _query.length()) {
            auto keyEnd = _query.find_first_of("=;&\0", readPos);
            if (keyEnd != std::string_view::npos && keyEnd < _query.length()) {
                auto key = decode(_query.substr(readPos, keyEnd - readPos));
                if (_query[keyEnd] != '=') {
                    _queryMap[key] = std::nullopt;
                    readPos        = keyEnd + 1; //+1 for separator character
                    continue;
                }
                readPos = keyEnd + 1; // skip equal after '='
                // equal sign present
                if (readPos >= _query.length()) {
                    // reached parameter string end
                    _queryMap[key] = std::nullopt;
                    break;
                }
                const auto valueEnd = _query.find_first_of(";&\0", readPos);
                if (valueEnd != std::string_view::npos && valueEnd < _query.length()) {
                    _queryMap[key] = std::optional(decode(_query.substr(readPos, valueEnd - readPos)));
                    readPos        = valueEnd + 1;
                    continue;
                }
                const auto value = decode(_query.substr(readPos, _query.length() - readPos));
                _queryMap[key] = value.empty() ? std::nullopt : std::optional(value);
                break;
            } else {
                auto key       = std::string(_query.substr(readPos, _query.length() - readPos));
                _queryMap[key] = std::nullopt;
                break;
            }
        }

        return _queryMap;
    };

    // comparison operators
    auto operator<=>(const URI &other) const noexcept { return _str <=> other.str(); }
    bool operator==(const URI &other) const noexcept  { return _str == other.str(); }

    class UriFactory {
        mutable std::string     _authority;
        std::string             _scheme;
        std::string             _userName;
        std::string             _pwd;
        std::string             _host;
        std::optional<uint16_t> _port;
        std::string             _path;
        mutable std::string     _query;
        std::string             _fragment;
        // local map to overwrite _query parameter if set
        std::unordered_map<std::string, std::optional<std::string>> _queryMap;

    public:
        UriFactory() = default;
        UriFactory(const UriFactory &) = default;
        UriFactory& operator=(const UriFactory &) = default;

        UriFactory copy() const {
            return UriFactory(*this);
        }

        // clang-format off
        explicit UriFactory(const URI &uri) {
            if (!uri._scheme.empty())    { _scheme = uri._scheme; }
            if (!uri._authority.empty()) { _authority = uri._authority; }
            if (!uri._userName.empty())  { _userName = uri._userName; }
            if (!uri._pwd.empty())       { _pwd = uri._pwd; }
            if (!uri._hostName.empty())  { _host = uri._hostName; }
            if (!uri._port.empty())      { _port = std::stoi(std::string(uri._port)); }
            if (!uri._path.empty())      { _path = uri._path; }
            if (!uri._query.empty())     { _query = uri._query; }
            if (!uri._fragment.empty())  { _fragment = uri._fragment; }
        }
        inline UriFactory&& scheme(const std::string_view &scheme)        && noexcept { _scheme = scheme; return std::move(*this); }
        inline UriFactory&& authority(const std::string_view &authority)  && noexcept { _authority = authority; return std::move(*this); }
        inline UriFactory&& user(const std::string_view &userName)        && noexcept { _userName = userName; return std::move(*this); }
        inline UriFactory&& password(const std::string_view &pwd)         && noexcept { _pwd = pwd; return std::move(*this); }
        inline UriFactory&& hostName(const std::string_view &hostName)    && noexcept { _host = hostName; return std::move(*this); }
        inline UriFactory&& port(const uint16_t port)                     && noexcept { _port = std::optional<uint16_t>(port); return std::move(*this); }
        inline UriFactory&& path(const std::string_view &path)            && noexcept { _path = path; return std::move(*this); }
        inline UriFactory&& queryParam(const std::string_view &query)     && noexcept { _query = query; return std::move(*this); }
        inline UriFactory&& fragment(const std::string_view &fragment)    && noexcept { _fragment = fragment; return std::move(*this); }
        inline UriFactory&& addQueryParameter(const std::string &key)     && noexcept { _queryMap[key] = std::nullopt; return std::move(*this); }
        inline UriFactory&& addQueryParameter(const std::string &key, const std::string &value) && noexcept { _queryMap[key] = std::optional(value); return std::move(*this); }
        inline UriFactory&& setQuery(std::unordered_map<std::string, std::optional<std::string>> queryMap) && noexcept { _query.clear(); _queryMap = std::move(queryMap); return std::move(*this); }
        // clang-format on

        std::string toString() const {
            using namespace fmt::literals;
            if (_authority.empty()) {
                _authority = fmt::format("{user}{opt_colon1}{pwd}{at}{host}{opt_colon2}{port}",                              //
                        fmt::arg("user", _userName), fmt::arg("opt_colon1", (_pwd.empty() || _userName.empty()) ? "" : ":"), /* user:pwd colon separator */
                        fmt::arg("pwd", _userName.empty() ? "" : _pwd), fmt::arg("at", _userName.empty() ? "" : "@"),        // 'user:pwd@' separator
                        fmt::arg("host", _host), fmt::arg("opt_colon2", _port ? ":" : ""), fmt::arg("port", _port ? std::to_string(_port.value()) : ""));
            }

            // TODO: Calling toString multiple times appends parameters over and over again
            for (const auto &[key, value] : _queryMap) {
                _query += fmt::format("{opt_ampersand}{key}{opt_equal}{value}",                             // N.B. 'key=value' percent-encoding according to RFC 3986
                        fmt::arg("opt_ampersand", _query.empty() ? "" : "&"), fmt::arg("key", encode(key)), //
                        fmt::arg("opt_equal", value ? "=" : ""), fmt::arg("value", value ? encode(value.value()) : ""));
            }

            return fmt::format("{scheme}{colon}{opt_auth_slash}{authority}{opt_path_slash}{path}{qMark}{query}{hashMark}{fragment}",                 //
                    fmt::arg("scheme", _scheme), fmt::arg("colon", _scheme.empty() ? "" : ":"),                                                      // scheme
                    fmt::arg("opt_auth_slash", (_authority.empty() || _authority.starts_with("//")) ? "" : "//"), fmt::arg("authority", _authority), // authority
                    fmt::arg("opt_path_slash", (_path.empty() || _path.starts_with('/') || _authority.empty()) ? "" : "/"), fmt::arg("path", _path), // path
                    fmt::arg("qMark", (_query.empty() || _query.starts_with('?')) ? "" : "?"), fmt::arg("query", _query),                            // query
                    fmt::arg("hashMark", (_fragment.empty() || _fragment.starts_with('#')) ? "" : "#"), fmt::arg("fragment", encode(_fragment)));    // fragment
        }

        URI build() {
            return URI<check>(toString());
        }
    };

    static inline UriFactory factory() noexcept { return UriFactory(); }
    static inline UriFactory factory(const URI &uri) noexcept { return UriFactory(uri); }

private:
    inline std::optional<std::string> returnOpt(const std::string_view &src) const noexcept { // simple optional un-wrapper
        if (!src.empty()) {
            assert(src.data() >= _str.data() && src.data() < _str.data() + _str.size());
        }
        return src.empty() ? std::nullopt : std::optional<std::string>(src);
    };

    inline void parseURI() {
        std::string_view source(_str.c_str(), _str.size());
        if constexpr (check == STRICT) {
            constexpr auto validURICharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=%";
            unsigned long  illegalChar        = source.find_first_not_of(validURICharacters);
            if (illegalChar != std::string::npos) {
                throw URISyntaxException(fmt::format("URI contains illegal characters: {} at position {} of {}", source, illegalChar, source.length()));
            }
        }

        if (source.size() == 0) {
            return;
        }

        const auto  pathOnly   = source[0] == '/';

        std::size_t schemeSize = 0UL;
        if (!pathOnly) {
            // check for scheme
            schemeSize                      = source.find_first_of(':', 0);
            const std::size_t nextSeparator = source.find_first_of("/?#", 0);
            if (schemeSize < nextSeparator && schemeSize != std::string::npos) {
                _scheme = source.substr(0, schemeSize);
                if constexpr (check == STRICT) {
                    if (!std::all_of(_scheme.begin(), _scheme.end(), [](char c) { return std::isalnum(c); })) {
                        throw URISyntaxException(fmt::format("URI scheme contains illegal characters: {}", _scheme));
                    }
                }
                schemeSize++;
            } else {
                schemeSize = 0UL;
            }
        }

        // check for authority
        std::size_t authEnd = 0UL;
        if (!pathOnly) {
            std::size_t authOffset = schemeSize;
            authEnd                = schemeSize;
            if (authEnd == source.length()) {
                return; // nothing more to parse
            }
            if ((source.length() > (authOffset + 1) && source[schemeSize] == '/' && source[schemeSize + 1] == '/')
                    || (schemeSize == 0 && source[schemeSize] != '/' && source[schemeSize] != '?' && source[schemeSize] != '#')) {
                // authority isUnreserved defined starting with '//'
                authOffset += 2;
                authEnd    = std::min(source.find_first_of("/?#", authOffset), source.length());
                _authority = source.substr(authOffset, authEnd - authOffset);
                if constexpr (check == STRICT) {
                    if (!std::all_of(_authority.begin(), _authority.end(), [](char c) { return std::isalnum(c) || c == '@' || c == ':' || c == '.' || c == '-' || c == '_'; })) {
                        throw URISyntaxException(fmt::format("URI authority contains illegal characters: {}", _authority));
                    }
                }
                // lazy parsing of authority in parseAuthority()
            } else {
                authEnd = schemeSize;
            }
        }

        size_t pathEnd = std::min(source.find_first_of("?#", authEnd), source.length());
        if (pathEnd <= source.length()) {
            _path = source.substr(authEnd, pathEnd - authEnd);
            if constexpr (check == STRICT) {
                if (!std::all_of(_path.begin(), _path.end(), [](char c) { return std::isalnum(c) || c == '/' || c == '.' || c == '-' || c == '_'; })) {
                    throw URISyntaxException(fmt::format("URI path contains illegal characters: {}", _path));
                }
            }
        } else {
            // no path info
        }
        if (pathEnd == source.length()) {
            return; // nothing more to parse
        }
        size_t queryEnd = std::min(source.find_first_of('#', pathEnd), source.length());
        if (source[pathEnd] == '?' && queryEnd != std::string_view::npos && queryEnd <= source.length()) {
            _query = source.substr(pathEnd + 1, queryEnd - pathEnd - 1);
        } else {
            // no query present
            queryEnd = pathEnd;
        }
        if (queryEnd == source.length()) {
            return; // nothing more to parse
        }
        size_t fragStart = source.find_first_of('#', queryEnd);
        if (fragStart != std::string_view::npos && fragStart < source.length()) {
            fragStart++;
            _fragment = source.substr(fragStart, source.length() - fragStart);
        }
    }

    // returns tif only RFC 3986 section 2.3 Unreserved Characters
    static constexpr inline bool isUnreserved(const char c) noexcept { return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~'; }
    constexpr inline void        parseAuthority() const {
        if (_parsedAuthority || _authority.empty()) {
            _parsedAuthority = true;
            return;
        }
        size_t userSplit = std::min(_authority.find_first_of('@'), _authority.length());
        if (userSplit < _authority.length()) {
            // user isUnreserved defined via '[user]:[pwd]@'
            const size_t pwdSplit = std::min(_authority.find_first_of(':'), userSplit);
            _userName             = _authority.substr(0, pwdSplit);
            if (pwdSplit < userSplit) {
                _pwd = _authority.substr(pwdSplit + 1, userSplit - pwdSplit - 1);
            }
            userSplit++;
        } else {
            userSplit = 0;
        }
        size_t portSplit = std::min(_authority.find_first_of(':', userSplit), _authority.length());
        if (portSplit != std::string_view::npos && portSplit < _authority.length()) {
            // port defined
            _hostName = _authority.substr(userSplit, portSplit - userSplit);
            portSplit++;
            _port = _authority.substr(portSplit, _authority.length() - portSplit);
        } else {
            _hostName = _authority.substr(userSplit, _authority.length() - userSplit);
        }
    }
};
} // namespace opencmw

template<opencmw::uri_check check>
struct std::hash<opencmw::URI<check>> {
    std::size_t operator()(const opencmw::URI<check> &uri) const noexcept { return std::hash<std::string>{}(uri.str()); }
};

// fmt::format and std::ostream helper output

// fmt::format and std::ostream helper output for std::optional
template<typename T> // TODO: move to utils class
struct fmt::formatter<std::optional<T>> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin(); // not (yet) implemented
    }

    template<typename FormatContext>
    auto format(std::optional<T> const &v, FormatContext &ctx) const {
        return v ? fmt::format_to(ctx.out(), "'{}'", v.value()) : fmt::format_to(ctx.out(), "{{}}");
    }
};

template<typename T>
inline std::ostream &operator<<(std::ostream &os, const std::optional<T> &v) {
    return os << fmt::format("{}", v);
}

template<opencmw::uri_check check>
struct fmt::formatter<opencmw::URI<check>> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin(); // not (yet) implemented
    }

    template<typename FormatContext>
    auto format(opencmw::URI<check> const &v, FormatContext &ctx) const {
        return fmt::format_to(ctx.out(), "{{scheme: {}, authority: {}, user: {}, pwd: {}, host: {}, port: {}, path: {}, query: {}, fragment: {}}}", //
                v.scheme(), v.authority(), v.user(), v.password(), v.hostName(), v.port(), v.path(), v.queryParam(), v.fragment());
    }
};

template<opencmw::uri_check check>
inline std::ostream &operator<<(std::ostream &os, const opencmw::URI<check> &v) {
    return os << fmt::format("{}", v);
}

#endif // OPENCMW_CPP_URI_HPP
