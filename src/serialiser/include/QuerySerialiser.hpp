#ifndef OPENCMW_QUERYPARAMETERPARSER_HPP
#define OPENCMW_QUERYPARAMETERPARSER_HPP

#include <MIME.hpp>
#include <opencmw.hpp>
#include <TimingCtx.hpp>

#include <Filters.hpp>

#include <format>

#include <refl.hpp>

#include <charconv>
#include <exception>
#include <opencmw.hpp>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace opencmw::query {

using QueryMap = std::unordered_map<std::string, std::optional<std::string>>;

template<typename T>
struct QuerySerialiser {
    using FilterType = typename opencmw::DomainFilter<std::string_view>;
    // TODO passing the map here is mainly for the bool special case, where we
    // want to insert a nullopt to the map if the value is true.
    // Maybe we should handle that special case directly in the serialiser loop
    inline static void serialise(QueryMap &, const std::string &, const T &) {
        static_assert(opencmw::always_false<T>, "don't know how to serialise this type");
    }

    inline static void deserialise(const std::string &, T &) {
        static_assert(opencmw::always_false<T>, "don't know how to deserialise this type");
    }
};

template<StringLike T>
struct QuerySerialiser<T> {
    using FilterType = typename opencmw::DomainFilter<std::string_view>;

    inline static void serialise(QueryMap &m, const std::string &key, const T &value) {
        m[key] = value;
    }

    inline static void deserialise(const std::optional<std::string> &maybeStr, T &v) {
        if (maybeStr) {
            v = *maybeStr;
        }
    }
};

template<>
struct QuerySerialiser<bool> {
    // TODO this doesn't consider that an entry <key, std::nullopt> means "true"
    using FilterType = typename opencmw::DomainFilter<std::string_view>;

    inline static void serialise(QueryMap &m, const std::string &key, bool value) {
        if (value) {
            m[key] = std::nullopt;
        }
    }

    inline static void deserialise(const std::optional<std::string> &maybeStr, bool &v) {
        if (maybeStr && *maybeStr != "true" && *maybeStr != "false") {
            throw std::invalid_argument(std::format("Invalid boolean value '{}'", *maybeStr));
        }

        v = !maybeStr || *maybeStr == "true";
    }
};

template<Number T>
struct QuerySerialiser<T> {
    using FilterType = typename opencmw::NumberFilter<T>;

    inline static void serialise(QueryMap &m, const std::string &key, const T &value) {
        m[key] = std::to_string(value);
    }

    inline static void deserialise(const std::optional<std::string> &maybeStr, T &v) {
        if (!maybeStr) {
            throw std::invalid_argument("Empty string is not a valid number");
        }

        if (const auto rc = parseNumber(maybeStr.value(), v); rc == std::errc::invalid_argument) {
            throw std::invalid_argument(std::format("Cannot parse as number '{}'", *maybeStr));
        }
    }
};

template<>
struct QuerySerialiser<opencmw::TimingCtx> {
    using FilterType = typename opencmw::TimingCtxFilter;

    inline static void serialise(QueryMap &m, const std::string &key, const opencmw::TimingCtx &value) {
        m[key] = value.toString();
    }

    inline static void deserialise(const std::optional<std::string> &maybeStr, opencmw::TimingCtx &v) {
        if (!maybeStr) {
            throw std::invalid_argument("Empty string is not a valid TimingCtx");
        }

        v = opencmw::TimingCtx(*maybeStr);
    }
};

template<>
struct QuerySerialiser<opencmw::MIME::MimeType> {
    using FilterType = typename opencmw::ContentTypeFilter;

    inline static void serialise(QueryMap &m, const std::string &key, const opencmw::MIME::MimeType &value) {
        m[key] = std::string(value.typeName());
    }

    inline static void deserialise(const std::optional<std::string> &maybeStr, opencmw::MIME::MimeType &value) {
        if (!maybeStr) {
            throw std::invalid_argument("Empty string is not a valid MIME type");
        }

        value = opencmw::MIME::getType(*maybeStr);
    }
};

template<ReflectableClass C, MapLike TMap>
C deserialise(const TMap &m) {
    C context;

    for_each(refl::reflect(context).members, [&](const auto member, [[maybe_unused]] const auto index) {
        if constexpr (is_field(member) && !is_static(member)) {
            using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(context))))>;

            // TODO handle optionals?
            // TODO handle vectors? QueryMap would have to be a multimap

            static_assert(!is_smart_pointer<std::remove_reference_t<MemberType>>, "Pointer members not handled");

            const std::string fieldName{ member.name.c_str(), member.name.size };
            try {
                const auto it = m.find(fieldName);
                if (it != m.end()) {
                    QuerySerialiser<MemberType>::deserialise(it->second, member(context));
                }
            } catch (...) {
                // ignore field if it doesn't parse
                // TODO: or: do not silently ignore but abort whole parsing?
            }
        }
    });

    return context;
}

template<ReflectableClass C>
QueryMap serialise(const C &context) {
    QueryMap result;

    for_each(refl::reflect(context).members, [&](const auto member, [[maybe_unused]] const auto index) {
        if constexpr (is_field(member) && !is_static(member)) {
            using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(context))))>;

            // TODO handle optionals?
            // TODO handle vectors? QueryMap would have to be a multimap

            static_assert(!is_smart_pointer<std::remove_reference_t<MemberType>>, "Pointer members not handled");

            const std::string fieldName{ member.name.c_str(), member.name.size };
            QuerySerialiser<MemberType>::serialise(result, fieldName, member(context));
        }
    });

    return result;
}

template<ReflectableClass C, typename T>
void registerTypes(const C &context, T &registerAt) {
    for_each(refl::reflect(context).members, [&](const auto member, [[maybe_unused]] const auto index) {
        if constexpr (is_field(member) && !is_static(member)) {
            using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(context))))>;
            static_assert(!is_smart_pointer<std::remove_reference_t<MemberType>>, "Pointer members not handled");

            const std::string fieldName{ member.name.c_str(), member.name.size };

            using FilterType = typename QuerySerialiser<MemberType>::FilterType;
            registerAt.template addFilter<FilterType>(fieldName);
        }
    });
}

template<ReflectableClass C>
inline opencmw::MIME::MimeType getMimeType(const C &context) {
    constexpr auto type    = refl::reflect<C>();
    constexpr auto members = refl::descriptor::get_members(type);

    constexpr auto isMimeType
            = [&](auto member) {
                  using MemberType = std::remove_reference_t<decltype(getAnnotatedMember(unwrapPointer(member(context))))>;
                  return is_field(member) && !is_static(member) && std::same_as<MemberType, opencmw::MIME::MimeType>;
              };

    if constexpr (contains(members, isMimeType)) {
        return find_first(members, isMimeType)(context);
    }

    return opencmw::MIME::UNKNOWN;
}

} // namespace opencmw::query

#endif
