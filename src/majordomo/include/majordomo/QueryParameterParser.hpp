#ifndef OPENCMW_QUERYPARAMETERPARSER_HPP
#define OPENCMW_QUERYPARAMETERPARSER_HPP

#include <MIME.hpp>
#include <opencmw.hpp>

#include <optional>
#include <string_view>
#include <unordered_map>

namespace opencmw {

// TODO this is a placeholder mimicking the Java API, it shouldn't stay this way
class QueryParameterParser {
public:
    using QueryMap = std::unordered_map<std::string, std::optional<std::string>>;

    template<ReflectableClass C>
    static C parseQueryParameter(const QueryMap &) {
        C c;
        // TODO
        return c;
    }

    template<ReflectableClass C>
    static QueryMap generateQueryParameter(const C &) {
        return {}; // TODO
    }

    static opencmw::MIME::MimeType getMimeType(const std::unordered_map<std::string, std::optional<std::string>> &) {
        return opencmw::MIME::JSON; // TODO
    }
};

} // namespace opencmw

#endif
