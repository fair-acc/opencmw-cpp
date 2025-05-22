#pragma once

#include <exception>
#include <format>
#include <ostream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace opencmw::disruptor {

class DisruptorException : public std::exception {};

class WrappedException : public DisruptorException {
private:
    std::string           _message;
    const std::exception &_innerException;

public:
    explicit WrappedException(const std::exception &ex, std::string msg)
        : _message(std::move(msg)), _innerException(ex) {
        _message.append("\n\t");
        _message.append(ex.what());
    }
    const char    *what() const noexcept override { return _message.c_str(); }

    std::exception innerException() const {
        return _innerException;
    }
};

class NoCapacityException : public DisruptorException {};
class TimeoutException : public DisruptorException {};
class AlertException : public DisruptorException {};

} // namespace opencmw::disruptor
