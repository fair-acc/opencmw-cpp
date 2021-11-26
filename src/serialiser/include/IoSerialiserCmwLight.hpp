#ifndef OPENCMW_CMWLIGHTSERIALISER_H
#define OPENCMW_CMWLIGHTSERIALISER_H

#include "IoSerialiser.hpp"
#include <list>
#include <map>
#include <queue>

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-magic-numbers"
#pragma ide diagnostic   ignored "cppcoreguidelines-avoid-c-arrays"

namespace opencmw {
struct CmwLight : Protocol<"CmwLight"> {
};

namespace cmwlight {

}

template<Number T>
struct IoSerialiser<T, CmwLight> { // catch all template
    constexpr static bool serialise(IoBuffer &buffer, const ClassField &field, const T &value) noexcept {
        if (std::is_constant_evaluated()) {
            using namespace std::literals;
            buffer.put(field);
            buffer.template put(value);
            return true;
        }
        std::cout << fmt::format("{} - serialise-catch-all: {} {} value: {} - constexpr?: {}\n",
                CmwLight::protocolName(), typeName<T>, field, value,
                std::is_constant_evaluated());
        return false;
    }
};
} // namespace opencmw

#pragma clang diagnostic pop
#endif //OPENCMW_CMWLIGHTSERIALISER_H
