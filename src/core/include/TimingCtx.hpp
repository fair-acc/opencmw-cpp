#ifndef OPENCMW_CORE_TIMINGCTX_H
#define OPENCMW_CORE_TIMINGCTX_H

#include <fmt/format.h>

#include <charconv>
#include <chrono>
#include <exception>
#include <optional>
#include <string_view>

namespace opencmw {

class TimingCtx {
private:
    std::chrono::microseconds _bpcts;
    int                       _cid = WILDCARD_VALUE;
    int                       _sid = WILDCARD_VALUE;
    int                       _pid = WILDCARD_VALUE;
    int                       _gid = WILDCARD_VALUE;

public:
    explicit TimingCtx(std::chrono::microseconds bpcts = {})
        : _bpcts(bpcts) {}

    explicit TimingCtx(const std::optional<int> &cid, const std::optional<int> &sid, const std::optional<int> &pid, const std::optional<int> &gid, std::chrono::microseconds bpcts = {})
        : _bpcts(bpcts), _cid(cid.value_or(WILDCARD_VALUE)), _sid(sid.value_or(WILDCARD_VALUE)), _pid(pid.value_or(WILDCARD_VALUE)), _gid(gid.value_or(WILDCARD_VALUE)) {}

    explicit TimingCtx(std::string_view selector, std::chrono::microseconds bpcts = {})
        : _bpcts(bpcts) {
        if (selector.empty() || iequal(selector, WILDCARD)) {
            return;
        }

        if (selector.starts_with(SELECTOR_PREFIX)) { // TODO is a string without the prefix valid?
            selector.remove_prefix(SELECTOR_PREFIX.length());
        }

        if (iequal(selector, WILDCARD)) {
            return;
        }

        while (true) {
            const auto posColon = selector.find(':');
            const auto tag      = posColon != std::string_view::npos ? selector.substr(0, posColon) : selector;

            if (tag.length() < 3) {
                throw std::invalid_argument(fmt::format("Invalid tag '{}'", tag));
            }

            const auto posEqual = tag.find('=');

            // there must be one char left of the '=', at least one after, and there must be only one '='
            if (posEqual != 1 || tag.find('=', posEqual + 1) != std::string_view::npos) {
                throw std::invalid_argument(fmt::format("Tag has invalid format: '{}'", tag));
            }

            const auto key         = tag.substr(0, posEqual);
            const auto valueString = tag.substr(posEqual + 1, tag.length() - posEqual - 1);

            int        value       = -1;

            if (!iequal(WILDCARD, valueString)) {
                int        intValue = 0;
                const auto result   = std::from_chars(valueString.begin(), valueString.end(), intValue);

                if (result.ec == std::errc::invalid_argument) {
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
                throw std::invalid_argument(fmt::format("Unknown key '{}' in '{}'.", key[0], tag));
            }

            if (posColon == std::string_view::npos) {
                // if there's no other segment, we're done
                return;
            }

            // otherwise advance to after the ":"
            selector.remove_prefix(posColon + 1);
        }
    }

    std::chrono::microseconds bpcts() const { return _bpcts; }
    std::optional<int>        cid() const { return asOptional(_cid); }
    std::optional<int>        sid() const { return asOptional(_sid); }
    std::optional<int>        pid() const { return asOptional(_pid); }
    std::optional<int>        gid() const { return asOptional(_gid); }

    // these are not commutative, and must not be confused with operator==
    bool matches(const TimingCtx &other) const {
        return wildcardMatch(_cid, other._cid) && wildcardMatch(_sid, other._sid) && wildcardMatch(_pid, other._pid) && wildcardMatch(_gid, other._gid);
    }

    bool matchesWithBpcts(const TimingCtx &other) const {
        return _bpcts == other._bpcts && matches(other);
    }

    bool        operator==(const TimingCtx &) const = default;

    std::string toString() const {
        if (isWildcard(_cid) && isWildcard(_sid) && isWildcard(_pid) && isWildcard(_gid)) {
            auto s = std::string(SELECTOR_PREFIX);
            s.append(WILDCARD);
            return s;
        }

        auto formatValue = [](int v) {
            if (isWildcard(v)) {
                return std::string(WILDCARD);
            }

            return std::to_string(v);
        };

        return fmt::format("{}C={}:S={}:P={}:T={}", SELECTOR_PREFIX, formatValue(_cid), formatValue(_sid), formatValue(_pid), formatValue(_gid));
    }

private:
    static inline std::optional<int> asOptional(int x) {
        return x == WILDCARD_VALUE ? std::nullopt : std::optional<int>{ x };
    }

    static inline bool isWildcard(int x) {
        return x == -1;
    }

    constexpr static auto WILDCARD        = std::string_view("ALL");
    constexpr static auto WILDCARD_VALUE  = -1;
    constexpr static auto SELECTOR_PREFIX = std::string_view("FAIR.SELECTOR.");

    template<typename Left, typename Right>
    inline bool iequal(const Left &left, const Right &right) {
        return std::equal(std::cbegin(left), std::cend(left), std::cbegin(right), std::cend(right),
                [](auto l, auto r) { return std::tolower(l) == std::tolower(r); });
    }

    static inline bool wildcardMatch(int lhs, int rhs) {
        return isWildcard(rhs) || lhs == rhs;
    }
};

} // namespace opencmw

#endif
