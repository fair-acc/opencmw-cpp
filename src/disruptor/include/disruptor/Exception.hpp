#pragma once

#include <exception>
#include <fmt/format.h>
#include <ostream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace opencmw::disruptor {

class DisruptorException : public std::exception {};

class WrappedException : public DisruptorException {
private:
    std::string           message;
    const std::exception &inner_exception;

public:
    explicit WrappedException(const std::exception &ex, std::string msg)
        : message(std::move(msg)), inner_exception(ex) {
        message.append("\n\t");
        message.append(ex.what());
    }
    const char *what() { return message.c_str(); }
};

class NoCapacityException : public DisruptorException {};
class TimeoutException : public DisruptorException {};
class AlertException : public DisruptorException {};

} // namespace opencmw::disruptor
