#include "ProducerType.hpp"
#include "stdafx.hpp"

namespace std {

ostream &operator<<(ostream &stream, const opencmw::disruptor::ProducerType &value) {
    switch (value) {
    case opencmw::disruptor::ProducerType::Single:
        return stream << "Single";
    case opencmw::disruptor::ProducerType::Multi:
        return stream << "Multi";
    default:
        return stream << static_cast<int>(value);
    }
}

} // namespace std
