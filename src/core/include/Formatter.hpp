#ifndef OPENCMW_CPP_FORMATTER_HPP
#define OPENCMW_CPP_FORMATTER_HPP

#include <format>

namespace opencmw {

template<typename T>
concept string_like = std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> || std::is_convertible_v<T, std::string_view>;

template<typename R>
concept FormattableRange = std::ranges::range<R> && !string_like<std::remove_cvref_t<R>> && !std::is_array_v<std::remove_cvref_t<R>> && std::formattable<std::ranges::range_value_t<R>, char>;

template<std::ranges::input_range R>
    requires std::formattable<std::ranges::range_value_t<R>, char>
std::string join(const R &range, std::string_view sep = ", ") {
    std::string out;
    auto        it  = std::ranges::begin(range);
    const auto  end = std::ranges::end(range);
    if (it != end) {
        out += std::format("{}", *it);
        while (++it != end) {
            out += std::format("{}{}", sep, *it);
        }
    }
    return out;
}

} // namespace opencmw

#ifndef STD_FORMATTER_RANGE
#define STD_FORMATTER_RANGE
template<opencmw::FormattableRange R>
struct std::formatter<R, char> {
    char           separator = ',';

    constexpr auto parse(std::format_parse_context &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const R &range, FormatContext &ctx) const {
        auto out = ctx.out();
        std::format_to(out, "[");
        bool first = true;

        for (const auto &val : range) {
            if (!first) {
                std::format_to(out, "{} ", separator);
            } else {
                first = false;
            }
            std::format_to(out, "{}", val);
        }

        return std::format_to(out, "]");
    }
};
#endif // STD_FORMATTER_RANGE

#ifndef STD_FORMATTER_OPTIONAL
#define STD_FORMATTER_OPTIONAL
template<class T>
struct std::formatter<std::optional<T>> {
    std::formatter<T> value_formatter;

    constexpr auto    parse(format_parse_context &ctx) {
        return value_formatter.parse(ctx);
    }

    template<class FormatContext>
    auto format(std::optional<T> const &opt, FormatContext &ctx) const {
        if (opt.has_value()) {
            return value_formatter.format(opt.value(), ctx);
        }
        return std::format_to(ctx.out(), "nullopt");
    }
};
#endif // STD_FORMATTER_OPTIONAL

#ifndef STD_FORMATTER_PAIR
#define STD_FORMATTER_PAIR
template<typename T1, typename T2>
struct std::formatter<std::pair<T1, T2>, char> {
    constexpr auto parse(std::format_parse_context &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const std::pair<T1, T2> &p, FormatContext &ctx) const {
        return std::format_to(ctx.out(), "({}, {})", p.first, p.second);
    }
};
#endif // STD_FORMATTER_PAIR

#endif // OPENCMW_CPP_FORMATTER_HPP
