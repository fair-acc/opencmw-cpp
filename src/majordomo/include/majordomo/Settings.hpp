#ifndef OPENCMW_MAJORDOMO_SETTINGS_H
#define OPENCMW_MAJORDOMO_SETTINGS_H

#include <URI.hpp>
#include <chrono>

namespace opencmw::majordomo {

struct Settings {
    // shared
    int                       highWaterMark     = 0;
    int                       heartbeatLiveness = 3;
    std::chrono::milliseconds heartbeatInterval = std::chrono::milliseconds(1000);

    // broker
    std::chrono::milliseconds     clientTimeout = std::chrono::seconds(10);
    opencmw::URI<>                dnsAddress    = opencmw::URI<>("");

    // worker
    std::chrono::milliseconds workerReconnectInterval = std::chrono::milliseconds(2500);
};
} // namespace opencmw::majordomo

#endif
