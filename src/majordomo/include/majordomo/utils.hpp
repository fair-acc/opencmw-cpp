#ifndef UTILS_H
#define UTILS_H

#include <cctype>
#include <string_view>

namespace utils {

template<typename Left, typename Right>
bool iequal(const Left &left, const Right &right) {
    return std::equal(std::cbegin(left), std::cend(left), std::cbegin(right), std::cend(right),
            [](auto l, auto r) { return std::tolower(l) == std::tolower(r); });
}

} // namespace utils

#endif // UTILS_H
