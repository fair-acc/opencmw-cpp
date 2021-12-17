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
    using string      = std::string;
    using string_view = std::string_view;

public:
    const string str;

private:
    // need to keep a local, owning, and immutable copy of the source template
    // evaluate on demand and if available
    string_view         _scheme;
    string_view         _authority;
    mutable bool        _parsedAuthority = false;
    mutable string_view _userName;
    mutable string_view _pwd;
    mutable string_view _hostName;
    mutable string_view _port;
    string_view         _path;
    string_view         _query;
    string_view         _fragment;

    // computed on-demand
    mutable std::unordered_map<string, std::optional<string>> _queryMap;
    // simple optional un-wrapper
    inline const std::optional<string> returnOpt(const string_view &src) const noexcept {
        if (!src.empty()) {
            assert(src.data() >= str.data() && src.data() < str.data() + str.size());
        }
        return src.empty() ? std::nullopt : std::optional<string>(src);
    };

protected:
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
        if (portSplit != string_view::npos && portSplit < _authority.length()) {
            // port defined
            _hostName = _authority.substr(userSplit, portSplit - userSplit);
            portSplit++;
            _port = _authority.substr(portSplit, _authority.length() - portSplit);
        } else {
            _hostName = _authority.substr(userSplit, _authority.length() - userSplit);
        }
    }

public:
    URI() = delete;
    explicit URI(std::string src)
        : str(std::move(src)) {
        string_view source(str.c_str(), str.size());
        if constexpr (check == STRICT) {
            constexpr auto validURICharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=%";
            unsigned long  illegalChar        = source.find_first_not_of(validURICharacters);
            if (illegalChar != std::string::npos) {
                throw URISyntaxException(fmt::format("URI contains illegal characters: {} at position {} of {}", source, illegalChar, source.length()));
            }
        }

        // check for scheme
        size_t scheme_size = source.find_first_of(':', 0);
        if (scheme_size != string::npos) {
            _scheme = source.substr(0, scheme_size);
            if constexpr (check == STRICT) {
                if (!std::all_of(_scheme.begin(), _scheme.end(), [](char c) { return std::isalnum(c); })) {
                    throw URISyntaxException(fmt::format("URI scheme contains illegal characters: {}", _scheme));
                }
            }
            scheme_size++;
        } else {
            scheme_size = 0L;
        }

        // check for authority
        size_t authOffset = scheme_size;
        size_t authEnd    = scheme_size;
        if (authEnd == source.length()) {
            return; // nothing more to parse
        }
        if ((source.length() > (authOffset + 1) && source[scheme_size] == '/' && source[scheme_size + 1] == '/')
                || (scheme_size == 0 && source[scheme_size] != '/' && source[scheme_size] != '?' && source[scheme_size] != '#')) {
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
            authEnd = scheme_size;
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
        if (source[pathEnd] == '?' && queryEnd != string_view::npos && queryEnd <= source.length()) {
            _query = source.substr(pathEnd + 1, queryEnd - pathEnd - 1);
        } else {
            // no query present
            queryEnd = pathEnd;
        }
        if (queryEnd == source.length()) {
            return; // nothing more to parse
        }
        size_t fragStart = source.find_first_of('#', queryEnd);
        if (fragStart != string_view::npos && fragStart < source.length()) {
            fragStart++;
            _fragment = source.substr(fragStart, source.length() - fragStart);
        }
    }

    URI(const URI &other)
        : str(other.str), _parsedAuthority(other._parsedAuthority), _queryMap(other._queryMap) {
        auto adjustedView = [this, &other](std::string_view otherView) {
            return std::string_view(
                    str.data() + std::distance(other.str.data(), otherView.data()),
                    otherView.size());
        };
        _scheme    = adjustedView(other._scheme);
        _authority = adjustedView(other._authority);
        _userName  = adjustedView(other._userName);
        _pwd       = adjustedView(other._pwd);
        _hostName  = adjustedView(other._hostName);
        _port      = adjustedView(other._port);
        _path      = adjustedView(other._path);
        _query     = adjustedView(other._query);
        _fragment  = adjustedView(other._fragment);
    }

    URI               &operator=(const URI &other) = delete;

    bool               empty() const { return str.empty(); }

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
                std::istringstream iss(string(s.substr(i + 1, 2))); // TODO find smarter conversion
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
    inline const std::optional<string> scheme() const noexcept { return returnOpt(_scheme); }
    inline const std::optional<string> authority() const noexcept { return returnOpt(_authority); }
    inline const std::optional<string> user() const noexcept { parseAuthority(); return returnOpt(_userName); }
    inline const std::optional<string> password() const noexcept { parseAuthority(); return returnOpt(_pwd); }
    inline const std::optional<string> hostName() const noexcept { parseAuthority(); return returnOpt(_hostName); }
    inline const std::optional<uint16_t> port() const noexcept { parseAuthority(); return _port.empty() ? std::nullopt : std::optional(std::stoi(string{ _port.begin(), _port.end() }));}
    inline const std::optional<string> path() const noexcept { return returnOpt(_path); }
    inline const std::optional<string> queryParam() const noexcept { return returnOpt(_query); }
    inline const std::optional<string> fragment() const noexcept { return returnOpt(_fragment); }
    // clang-format om

    // decompose map
    inline const std::unordered_map<string, std::optional<string>> &queryParamMap() const {
        if (_query.empty() || !_queryMap.empty()) { // empty query parameter or already parsed
            return _queryMap;
        }
        if constexpr (check == STRICT) {
            if (!std::all_of(_query.begin(), _query.end(), [](char c) { return isUnreserved(c) || c == '&' || c == ';' || c == '=' || c == '%'; })) {
                throw std::exception(); // TODO: URIException("URI query contains illegal characters: {}")
            }
        }
        size_t readPos = 0;
        while (readPos < _query.length()) {
            auto keyEnd = _query.find_first_of("=;&\0", readPos);
            if (keyEnd != string_view::npos && keyEnd < _query.length()) {
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
                if (valueEnd != string_view::npos && valueEnd < _query.length()) {
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
    auto operator<=>(const URI &other) const noexcept { return str <=> other.str; }
    bool operator==(const URI &other) const noexcept  { return str == other.str; }

    class UriFactory {
        string                  _authority;
        string                  _scheme;
        string                  _userName;
        string                  _pwd;
        string                  _host;
        std::optional<uint16_t> _port;
        string                  _path;
        string                  _query;
        string                  _fragment;
        // local map to overwrite _query parameter if set
        std::unordered_map<string, std::optional<string>> _queryMap;

    public:
        UriFactory() = default;
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
        inline UriFactory&& scheme(const std::string_view &scheme)        && noexcept { _scheme = { scheme.begin(), scheme.end() }; return std::move(*this); }
        inline UriFactory&& authority(const std::string_view &authority)  && noexcept { _authority = { authority.begin(), authority.end() }; return std::move(*this); }
        inline UriFactory&& user(const std::string_view &userName)        && noexcept { _userName = { userName.begin(), userName.end() }; return std::move(*this); }
        inline UriFactory&& password(const std::string_view &pwd)         && noexcept { _pwd = { pwd.begin(), pwd.end() }; return std::move(*this); }
        inline UriFactory&& hostName(const std::string_view &hostName)    && noexcept { _host = { hostName.begin(), hostName.end() }; return std::move(*this); }
        inline UriFactory&& port(const uint16_t port)                     && noexcept { _port = std::optional<uint16_t>(port); return std::move(*this); }
        inline UriFactory&& path(const std::string_view &path)            && noexcept { _path = { path.begin(), path.end() }; return std::move(*this); }
        inline UriFactory&& queryParam(const std::string_view &query)     && noexcept { _query = { query.begin(), query.end() }; return std::move(*this); }
        inline UriFactory&& fragment(const std::string_view &fragment)    && noexcept { _fragment = { fragment.begin(), fragment.end() }; return std::move(*this); }
        inline UriFactory&& addQueryParameter(const std::string &key)     && noexcept { _queryMap[key] = std::nullopt; return std::move(*this); }
        inline UriFactory&& addQueryParameter(const std::string &key, const std::string &value) && noexcept { _queryMap[key] = std::optional(value); return std::move(*this); }
        inline UriFactory&& setQuery(std::unordered_map<std::string, std::optional<std::string>> queryMap) && noexcept { _query.clear(); _queryMap = std::move(queryMap); return std::move(*this); }
        // clang-format on

        std::string toString() {
            using namespace fmt::literals;
            if (_authority.empty()) {
                _authority = fmt::format("{user}{opt_colon1}{pwd}{at}{host}{opt_colon2}{port}",                //
                        "user"_a = _userName, "opt_colon1"_a = (_pwd.empty() || _userName.empty()) ? "" : ":", /* user:pwd colon separator */
                        "pwd"_a = _userName.empty() ? "" : _pwd, "at"_a = _userName.empty() ? "" : "@",        // 'user:pwd@' separator
                        "host"_a = _host, "opt_colon2"_a = _port ? ":" : "", "port"_a = _port ? std::to_string(_port.value()) : "");
            }

            for (const auto &[key, value] : _queryMap) {
                _query += fmt::format("{opt_ampersand}{key}{opt_equal}{value}",               // N.B. 'key=value' percent-encoding according to RFC 3986
                        "opt_ampersand"_a = _query.empty() ? "" : "&", "key"_a = encode(key), //
                        "opt_equal"_a = value ? "=" : "", "value"_a = value ? encode(value.value()) : "");
            }

            return fmt::format("{scheme}{colon}{opt_auth_slash}{authority}{opt_path_slash}{path}{qMark}{query}{hashMark}{fragment}",   //
                    "scheme"_a = _scheme, "colon"_a = _scheme.empty() ? "" : ":",                                                      // scheme
                    "opt_auth_slash"_a = (_authority.empty() || _authority.starts_with("//")) ? "" : "//", "authority"_a = _authority, // authority
                    "opt_path_slash"_a = (_path.empty() || _path.starts_with('/') || _authority.empty()) ? "" : "/", "path"_a = _path, // path
                    "qMark"_a = (_query.empty() || _query.starts_with('?')) ? "" : "?", "query"_a = _query,                            // query
                    "hashMark"_a = (_fragment.empty() || _fragment.starts_with('#')) ? "" : "#", "fragment"_a = encode(_fragment));    // fragment
        }

        URI build() {
            return URI<check>(toString());
        }
    };

    static inline UriFactory factory() noexcept { return UriFactory(); }
    static inline UriFactory factory(const URI &uri) noexcept { return UriFactory(uri); }
};
} // namespace opencmw

template<opencmw::uri_check check>
struct std::hash<opencmw::URI<check>> {
    std::size_t operator()(const opencmw::URI<check> &uri) const noexcept { return std::hash<std::string>{}(uri.str); }
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
    auto format(std::optional<T> const &v, FormatContext &ctx) {
        return v ? fmt::format_to(ctx.out(), "{}", v.value()) : fmt::format_to(ctx.out(), "<std::nullopt>"); // TODO: check alt of '<>' or ''
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
    auto format(opencmw::URI<check> const &v, FormatContext &ctx) {
        return fmt::format_to(ctx.out(), "{{scheme: '{}', authority: '{}', user: '{}', pwd: '{}', host: '{}', port: '{}', path: '{}', query: '{}', fragment: '{}'}}", //
                v.scheme(), v.authority(), v.user(), v.password(), v.hostName(), v.port(), v.path(), v.queryParam(), v.fragment());
    }
};

template<opencmw::uri_check check>
inline std::ostream &operator<<(std::ostream &os, const opencmw::URI<check> &v) {
    return os << fmt::format("{}", v);
}

#endif // OPENCMW_CPP_URI_HPP
