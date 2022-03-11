#ifndef OPENCMW_MAJORDOMO_SETTINGS_H
#define OPENCMW_MAJORDOMO_SETTINGS_H

#include <chrono>
#include <URI.hpp>

namespace opencmw::majordomo {

struct Settings {
    // shared
    int                       highWaterMark     = 0;
    int                       heartbeatLiveness = 3;
    std::chrono::milliseconds heartbeatInterval = std::chrono::milliseconds(1000);

    // broker
    std::chrono::milliseconds clientTimeout = std::chrono::seconds(10);
    std::string               dnsAddress;
    std::chrono::milliseconds dnsTimeout = std::chrono::seconds(10);

    // worker
    std::chrono::milliseconds workerReconnectInterval = std::chrono::milliseconds(2500);
};
} // namespace opencmw::majordomo

#endif
