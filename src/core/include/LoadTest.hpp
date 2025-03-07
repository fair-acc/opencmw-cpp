#ifndef OPENCMW_CPP_LOAD_TEST_HPP
#define OPENCMW_CPP_LOAD_TEST_HPP

#include <MIME.hpp>

#include <refl.hpp>

#include <chrono>
#include <string>

namespace opencmw::load_test {

struct Context {
    std::string             topic;
    std::int64_t            intervalMs  = 1000; // must be multiple of 10 (enforce?)
    std::int64_t            payloadSize = 100;
    std::int64_t            nUpdates    = -1; // -1 means infinite
    std::int64_t            initialDelayMs = 0;
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};

struct Payload {
    std::int64_t  index;
    std::string   data;
    std::int64_t timestampNs = 0;
};

inline std::chrono::nanoseconds timestamp() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());
}

} // namespace opencmw::load_test

ENABLE_REFLECTION_FOR(opencmw::load_test::Context, topic, intervalMs, payloadSize, nUpdates, initialDelayMs, contentType)
ENABLE_REFLECTION_FOR(opencmw::load_test::Payload, index, data, timestampNs)

#endif
