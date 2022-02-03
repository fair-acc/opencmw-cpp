#ifndef OPENCMW_UTILS_H
#define OPENCMW_UTILS_H
#include "IoSerialiser.hpp"
#include <array>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <string_view>
#include <type_traits>
#include <vector>

namespace opencmw::utils {

}

namespace opencmw {

// template<std::string_view const &...strings>
// struct join {
//     // Join all strings into a single std::array of chars
//     static constexpr auto impl() noexcept {
//         constexpr std::size_t     len = (strings.size() + ... + 0);
//         std::array<char, len + 1> concatenatedArray{};
//         auto                      append = [i = 0, &concatenatedArray](auto const &s) mutable {
//             for (auto c : s) {
//                 concatenatedArray[i++] = c;
//             }
//         };
//         (append(strings), ...);
//         concatenatedArray[len] = 0;
//         return concatenatedArray;
//     }
//     // Give the joined string static storage
//     static constexpr auto array = impl();
//     // View as a std::string_view
//     static constexpr std::string_view value{ array.data(), array.size() - 1 };
// };
//// Helper to get the value out
// template<std::string_view const &...string>
// static constexpr auto join_v = join<string...>::value;
//

} // namespace opencmw

#endif // OPENCMW_UTILS_H
