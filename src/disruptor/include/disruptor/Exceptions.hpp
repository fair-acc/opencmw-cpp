#pragma once

#include "ExceptionBase.hpp"

namespace opencmw::disruptor {

DISRUPTOR_DECLARE_EXCEPTION(AlertException);
DISRUPTOR_DECLARE_EXCEPTION(ArgumentException);
DISRUPTOR_DECLARE_EXCEPTION(ArgumentNullException);
DISRUPTOR_DECLARE_EXCEPTION(ArgumentOutOfRangeException);
DISRUPTOR_DECLARE_EXCEPTION(FatalException);
DISRUPTOR_DECLARE_EXCEPTION(InsufficientCapacityException);
DISRUPTOR_DECLARE_EXCEPTION(InvalidOperationException);
DISRUPTOR_DECLARE_EXCEPTION(NotSupportedException);
DISRUPTOR_DECLARE_EXCEPTION(TimeoutException);

} // namespace opencmw::disruptor

#define DISRUPTOR_THROW_ALERT_EXCEPTION() DISRUPTOR_THROW(::opencmw::disruptor::AlertException, "")

#define DISRUPTOR_THROW_ARGUMENT_EXCEPTION(message) DISRUPTOR_THROW(::opencmw::disruptor::ArgumentException, message)

#define DISRUPTOR_THROW_ARGUMENT_NULL_EXCEPTION(variable) \
    DISRUPTOR_PRAGMA_PUSH \
    DISRUPTOR_PRAGMA_IGNORE_CONDITIONAL_EXPRESSION_IS_CONSTANT \
    DISRUPTOR_PRAGMA_IGNORE_UNREACHABLE_CODE \
    do { \
        DISRUPTOR_THROW(::opencmw::disruptor::ArgumentNullException, "The variable '" << #variable << "' is null"); \
    } while (0); \
    DISRUPTOR_PRAGMA_POP

#define DISRUPTOR_THROW_ARGUMENT_OUT_OF_RANGE_EXCEPTION(variable) \
    DISRUPTOR_PRAGMA_PUSH \
    DISRUPTOR_PRAGMA_IGNORE_CONDITIONAL_EXPRESSION_IS_CONSTANT \
    DISRUPTOR_PRAGMA_IGNORE_UNREACHABLE_CODE \
    do { \
        DISRUPTOR_THROW(::opencmw::disruptor::ArgumentOutOfRangeException, "The variable '" << #variable << "' (" << variable << ") is out of range"); \
    } while (0); \
    DISRUPTOR_PRAGMA_POP

#define DISRUPTOR_THROW_ARGUMENT_OUT_OF_RANGE_WITH_MESSAGE_EXCEPTION(message) DISRUPTOR_THROW(::opencmw::disruptor::ArgumentOutOfRangeException, message)

#define DISRUPTOR_THROW_FATAL_EXCEPTION(disruptorMessage, innerException) \
    DISRUPTOR_PRAGMA_PUSH \
    DISRUPTOR_PRAGMA_IGNORE_CONDITIONAL_EXPRESSION_IS_CONSTANT \
    do { \
        std::stringstream disruptorStream; \
        disruptorStream << disruptorMessage; \
        throw ::opencmw::disruptor::FatalException(disruptorStream.str(), innerException, __FUNCTION__, __FILE__, __LINE__); \
    } while (0); \
    DISRUPTOR_PRAGMA_POP

#define DISRUPTOR_THROW_INSUFFICIENT_CAPACITY_EXCEPTION() DISRUPTOR_THROW(::opencmw::disruptor::InsufficientCapacityException, "")

#define DISRUPTOR_THROW_INVALID_OPERATION_EXCEPTION(message) DISRUPTOR_THROW(::opencmw::disruptor::InvalidOperationException, message)

#define DISRUPTOR_THROW_NOT_SUPPORTED_EXCEPTION() DISRUPTOR_THROW(::opencmw::disruptor::NotSupportedException, "")

#define DISRUPTOR_THROW_TIMEOUT_EXCEPTION() DISRUPTOR_THROW(::opencmw::disruptor::TimeoutException, "")
