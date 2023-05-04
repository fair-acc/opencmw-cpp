#ifndef OPENCMW_MAJORDOMO_SETTINGS_H
#define OPENCMW_MAJORDOMO_SETTINGS_H

#include <MdpMessage.hpp>
#include <URI.hpp>

#include <chrono>

namespace opencmw::majordomo {

struct Settings : public mdp::Settings {
    // broker
    std::chrono::milliseconds clientTimeout = std::chrono::seconds(10);
    std::string               dnsAddress;
    std::chrono::milliseconds dnsTimeout = std::chrono::seconds(10);

    // worker
    std::chrono::milliseconds workerReconnectInterval = std::chrono::milliseconds(2500);
};
} // namespace opencmw::majordomo

#endif
