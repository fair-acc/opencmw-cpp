#pragma once

namespace opencmw::disruptor {
/**
 * Defines producer types to support creation of RingBuffer with correct sequencer and publisher.
 */
enum class ProducerType {
    /**
     * Create a RingBuffer with a single event publisher to the RingBuffer
     */
    Single,

    /**
     * Create a RingBuffer supporting multiple event publishers to the one RingBuffer
     */
    Multi
};

} // namespace opencmw::disruptor

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
