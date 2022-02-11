#pragma once

#include <fmt/format.h>
#include <exception>
#include <ostream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace opencmw::disruptor {

class disruptor_exception : public std::exception {};

class wrapped_exception : public disruptor_exception {
private:
    std::string           message;
    const std::exception &inner_exception;
public:
    explicit wrapped_exception(const std::exception&ex, std::string msg): message(std::move(msg)), inner_exception(ex) {
        message.append("\n\t");
        message.append(ex.what());
    }
    const char* what() {return message.c_str();}
};

class no_capacity_exception : public disruptor_exception {};
class timeout_exception: public disruptor_exception {};
class alert_exception: public disruptor_exception {};

} // namespace opencmw::disruptor
