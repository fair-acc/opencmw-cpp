#ifndef OPENCMW_CORE_TIMINGCTX_H
#define OPENCMW_CORE_TIMINGCTX_H

#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <exception>
#include <opencmw.hpp>
#include <ranges>
#include <string_view>

namespace opencmw {

namespace detail {
constexpr uint32_t const_hash(char const *input) noexcept { return *input ? static_cast<uint32_t>(*input) + 33 * const_hash(input + 1) : 5381; } // NOLINT
} // namespace detail

class TimingCtx {
    constexpr static auto DEFAULT         = "FAIR.SELECTOR.ALL";
    constexpr static auto WILDCARD        = std::string_view("ALL");
    constexpr static auto WILDCARD_VALUE  = -1;
    constexpr static auto SELECTOR_PREFIX = std::string_view("FAIR.SELECTOR.");
    constexpr static auto EMPTY_HASH      = detail::const_hash(DEFAULT);
    mutable std::size_t   _hash           = EMPTY_HASH; // to distinguish already parsed selectors
    mutable int32_t       _cid            = WILDCARD_VALUE;
    mutable int32_t       _sid            = WILDCARD_VALUE;
    mutable int32_t       _pid            = WILDCARD_VALUE;
    mutable int32_t       _gid            = WILDCARD_VALUE;

public:
    std::string               selector = DEFAULT;
    std::chrono::microseconds bpcts    = {};

    TimingCtx(const std::string_view &selectorToken, std::chrono::microseconds bpcTimeStamp = {})
        : selector(toUpper(selectorToken)), bpcts(bpcTimeStamp) { parse(); }

    explicit TimingCtx(const int32_t cid = WILDCARD_VALUE, const int32_t sid = WILDCARD_VALUE, const int32_t pid = WILDCARD_VALUE, const int32_t gid = WILDCARD_VALUE, std::chrono::microseconds bpcTimeStamp = {})
        : _cid(cid), _sid(sid), _pid(pid), _gid(gid), selector(toString<false>()), bpcts(bpcTimeStamp) { parse(); }

    // clang-format off
    [[nodiscard]] int32_t cid() const { parse(); return _cid; }
    [[nodiscard]] int32_t sid() const { parse(); return _sid; }
    [[nodiscard]] int32_t pid() const { parse(); return _pid; }
    [[nodiscard]] int32_t gid() const { parse(); return _gid; }

    // these are not commutative, and must not be confused with operator==
    [[nodiscard]] bool matches(const TimingCtx &other) const { parse(); return wildcardMatch(_cid, other._cid) && wildcardMatch(_sid, other._sid) && wildcardMatch(_pid, other._pid) && wildcardMatch(_gid, other._gid); }
    [[nodiscard]] bool matchesWithBpcts(const TimingCtx &other) const { parse(); return bpcTimeStampMatch(bpcts, other.bpcts) && matches(other); }
    // clang-format on

    [[nodiscard]] auto operator<=>(const TimingCtx &other) const {
        parse();
        return std::tie(_cid, _sid, _pid, _gid, bpcts) <=> std::tie(other._cid, other._sid, other._pid, other._gid, other.bpcts);
    }
    [[nodiscard]] bool operator==(const TimingCtx &other) const {
        parse();
        return bpcts.count() == other.bpcts.count() && _cid == other._cid && _sid == other._sid && _pid == other._pid && _gid == other._gid;
    }

    template<bool forceParse = true>
    [[nodiscard]] std::string toString() const noexcept(forceParse) {
        if constexpr (forceParse) {
            parse();
        }
        if (isWildcard(_cid) && isWildcard(_sid) && isWildcard(_pid) && isWildcard(_gid)) {
            auto s = std::string(SELECTOR_PREFIX);
            s.append(WILDCARD);
            return s;
        }

        std::vector<std::string> segments;
        segments.reserve(4);
        // clang-format off
        if (!isWildcard(_cid)) { segments.emplace_back(fmt::format("C={}", _cid)); }
        if (!isWildcard(_sid)) { segments.emplace_back(fmt::format("S={}", _sid)); }
        if (!isWildcard(_pid)) { segments.emplace_back(fmt::format("P={}", _pid)); }
        if (!isWildcard(_gid)) { segments.emplace_back(fmt::format("T={}", _gid)); }
        // clang-format on
        return fmt::format("{}{}", SELECTOR_PREFIX, fmt::join(segments, ":"));
    }

    void parse() const {
        // lazy revaluation in case selector changed -- not mathematically perfect but should be sufficient given the limited/constraint selector syntax
        const size_t selectorHash = detail::const_hash(selector.data());
        if (_hash == selectorHash) {
            return;
        }
        _cid                                = WILDCARD_VALUE;
        _sid                                = WILDCARD_VALUE;
        _pid                                = WILDCARD_VALUE;
        _gid                                = WILDCARD_VALUE;
        _hash                               = selectorHash;
        const std::string upperCaseSelector = toUpper(selector);
        if (upperCaseSelector.empty() || upperCaseSelector == WILDCARD) {
            return;
        }

        if (!upperCaseSelector.starts_with(SELECTOR_PREFIX)) {
            throw std::invalid_argument(fmt::format("Invalid tag '{}'", selector));
        }
        auto upperCaseSelectorView = std::string_view{ upperCaseSelector.data() + SELECTOR_PREFIX.length(), upperCaseSelector.size() - SELECTOR_PREFIX.length() };

        if (upperCaseSelectorView == WILDCARD) {
            return;
        }

        while (true) {
            const auto posColon = upperCaseSelectorView.find(':');
            const auto tag      = posColon != std::string_view::npos ? upperCaseSelectorView.substr(0, posColon) : upperCaseSelectorView;

            if (tag.length() < 3) {
                _hash = 0;
                throw std::invalid_argument(fmt::format("Invalid tag '{}'", tag));
            }

            const auto posEqual = tag.find('=');

            // there must be one char left of the '=', at least one after, and there must be only one '='
            if (posEqual != 1 || tag.find('=', posEqual + 1) != std::string_view::npos) {
                _hash = 0;
                throw std::invalid_argument(fmt::format("Tag has invalid format: '{}'", tag));
            }

            const auto key         = tag.substr(0, posEqual);
            const auto valueString = tag.substr(posEqual + 1, tag.length() - posEqual - 1);

            int32_t    value       = -1;

            if (WILDCARD != valueString) {
                int32_t intValue = 0;
                if (const auto result = std::from_chars(valueString.begin(), valueString.end(), intValue); result.ec == std::errc::invalid_argument) {
                    _hash = 0;
                    throw std::invalid_argument(fmt::format("Value: '{}' in '{}' is not a valid integer", valueString, tag));
                }

                value = intValue;
            }

            switch (key[0]) {
            case 'C':
                _cid = value;
                break;
            case 'S':
                _sid = value;
                break;
            case 'P':
                _pid = value;
                break;
            case 'T':
                _gid = value;
                break;
            default:
                _hash = 0;
                throw std::invalid_argument(fmt::format("Unknown key '{}' in '{}'.", key[0], tag));
            }

            if (posColon == std::string_view::npos) {
                // if there's no other segment, we're done
                return;
            }

            // otherwise, advance to after the ":"
            upperCaseSelectorView.remove_prefix(posColon + 1);
        }
    }

private:
    [[nodiscard]] static constexpr bool isWildcard(int x) noexcept { return x == -1; }
    [[nodiscard]] static constexpr bool wildcardMatch(int lhs, int rhs) { return isWildcard(rhs) || lhs == rhs; }
    [[nodiscard]] static constexpr bool bpcTimeStampMatch(std::chrono::microseconds lhs, std::chrono::microseconds rhs) { return rhs.count() == 0 || lhs.count() == rhs.count(); }
    static inline std::string           toUpper(const std::string_view &mixedCase) noexcept {
        std::string retval;
        retval.resize(mixedCase.size());
        std::ranges::transform(mixedCase, retval.begin(), [](char c) noexcept { return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c; });
        return retval;
    }
};

} // namespace opencmw
// ENABLE_REFLECTION_FOR(opencmw::TimingCtx, selector, bpcts); //TODO: refactor bpcts to Annotated/mp-units api and re-enable bpcts
ENABLE_REFLECTION_FOR(opencmw::TimingCtx, selector);

template<>
struct fmt::formatter<opencmw::TimingCtx> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin(); // not (yet) implemented
    }

    template<typename FormatContext>
    auto format(const opencmw::TimingCtx &v, FormatContext &ctx) {
        return fmt::format_to(ctx.out(), "{}", v.toString());
    }
};

inline std::ostream &operator<<(std::ostream &os, const opencmw::TimingCtx &v) {
    return os << fmt::format("{}", v);
}

#endif
