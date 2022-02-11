#pragma once

#include <exception>
#include <ostream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace opencmw::disruptor {

class nested_exception : public std::exception {
private:
    std::string           message;
    const std::exception &inner_exception;

public:
    explicit nested_exception(const std::exception& inner_exception, std::string message): message(std::move(message)), inner_exception(inner_exception) {}
};

// AlertException;
// ArgumentException;
// ArgumentNullException;
// ArgumentOutOfRangeException;
// FatalException;
// InsufficientCapacityException;
// InvalidOperationException;
// NotSupportedException;
// TimeoutException;

template<typename TException>
class exception : public std::exception {
private:
    std::string           m_message;
    const std::exception &m_innerException;
    std::string           m_function;
    std::string           m_file;
    int                   m_line;
    mutable std::string   m_wholeMessage;

public:
    explicit exception(std::string message  = std::string(),
            std::string                function = std::string(),
            std::string                file     = std::string(),
            int                        line     = -1)
        : m_message(std::move(message))
        , m_innerException(emptyException())
        , m_function(std::move(function))
        , m_file(std::move(file))
        , m_line(line)
        , m_wholeMessage() {
    }

    explicit exception(const std::exception &innerException,
            std::string                          function = std::string(),
            std::string                          file     = std::string(),
            int                                  line     = -1)
        : m_message()
        , m_innerException(innerException)
        , m_function(std::move(function))
        , m_file(std::move(file))
        , m_line(line)
        , m_wholeMessage() {
    }

    explicit exception(std::string message,
            const std::exception      &innerException,
            std::string                function = std::string(),
            std::string                file     = std::string(),
            int                        line     = -1)
        : m_message(std::move(message))
        , m_innerException(innerException)
        , m_function(std::move(function))
        , m_file(std::move(file))
        , m_line(line)
        , m_wholeMessage() {
    }

    // theoricaly not necessary but g++ complains
    virtual ~exception() throw() {}

    const char *what() const throw() override {
        if (m_wholeMessage.empty())
            formatWholeMessage();

        return m_wholeMessage.c_str();
    }

    const std::string    &message() const { return m_message; }
    const std::string    &function() const { return m_function; }
    const std::string    &file() const { return m_file; }
    int                   line() const { return m_line; }
    const std::exception &innerException() const { return m_innerException; }

    // this stuff provides cool writing such as `throw Exception() << "Holy shit! what's wrong with id: " << id`;
    template<typename T>
    TException &operator<<(const T &rhs) {
        std::stringstream stream;
        stream << rhs;
        m_message += stream.str();
        return static_cast<TException &>(*this);
    }

protected:
    bool hasThrowLocationInformation() const {
        return m_line != -1;
    }

    void formatWholeMessage() const {
        std::stringstream stream;

        stream << m_message;

        if (hasThrowLocationInformation()) {
            stream << " - ";
            appendThrowLocationInformation(stream);
        }

        auto &&derivedMessage = getDerivedExceptionMessage();
        if (derivedMessage.size())
            stream << " - " << derivedMessage;

        m_wholeMessage = stream.str();
    }

    // let a chance for a derived exception to provide additional information, such as an api error string.
    virtual std::string getDerivedExceptionMessage() const {
        return {};
    }

    void appendThrowLocationInformation(std::stringstream &stream) const {
        stream << "Throw location: " << m_function << " in " << m_file << "(" << m_line << ")";
    }

    static const std::exception &emptyException() {
        static std::exception result;
        return result;
    }
};

template<typename TException>
inline std::string toString(const exception<TException> &ex) {
    std::stringstream stream;
    stream << ex;
    return stream.str();
}

template<typename TException>
inline std::ostream &operator<<(std::ostream &os, const opencmw::disruptor::exception<TException> &ex) {
    return os
            << ex.message()
            << ", InnerException: " << ex.innerException().what();
}

} // namespace opencmw::disruptor
