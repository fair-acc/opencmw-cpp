#pragma once

#include <cstdint>

namespace opencmw::disruptor {

/**
 * Translate a data representation into fields set in given event
 *
 * \param event into which the data should be translated.
 * \param sequence that is assigned to event.
 * \param args The array of user arguments.
 */
template<typename T, typename... TArgs>
class IEventTranslatorVararg {
public:
    virtual ~IEventTranslatorVararg() = default;

    /**
     * Translate a data representation into fields set in given event
     *
     * \param eventData event into which the data should be translated.
     * \param sequence sequence that is assigned to event.
     */
    virtual void translateTo(T &eventData, std::int64_t sequence, const TArgs &...args) = 0;
};

} // namespace opencmw::disruptor
