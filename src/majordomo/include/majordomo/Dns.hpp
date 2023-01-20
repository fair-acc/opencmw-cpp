#ifndef OPENCMW_CPP_DNS_H
#define OPENCMW_CPP_DNS_H

#include <fmt/core.h>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <iomanip>
#include <IoSerialiserJson.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/SubscriptionMatcher.hpp>
#include <majordomo/Utils.hpp>
#include <majordomo/Worker.hpp>
#include <majordomo/ZmqPtr.hpp>
#include <opencmw.hpp>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <QuerySerialiser.hpp>

#include "URI.hpp"

using namespace std::string_literals;
using namespace opencmw::majordomo;
using namespace std::chrono_literals;

struct Request {
    std::string brokerName;
    std::string serviceName;
    std::string signalName;
    // std::unordered_map<std::string, std::string> meta;
};
ENABLE_REFLECTION_FOR(Request, brokerName, serviceName, signalName)

struct Reply {
    // std::set<opencmw::URI<opencmw::RELAXED>> uris;
    std::vector<std::string> uris;
    // std::vector<KeyValue> meta;
    //  std::unordered_map<std::string, std::vector<std::string>> meta;
};
ENABLE_REFLECTION_FOR(Reply, uris)

struct DnsContext {
    opencmw::TimingCtx      ctx;
    std::string             signalFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::JSON;
};
ENABLE_REFLECTION_FOR(DnsContext, signalFilter, contentType)

namespace opencmw::DNS {
using opencmw::majordomo::Broker;
using BrokerMessage = opencmw::majordomo::BasicMdpMessage<
        opencmw::majordomo::MessageFormat::WithSourceId>;

namespace detail {
struct DnsServiceInfo {
    std::set<std::string> uris;
    // std::set<std::string> signalNames;
    //  std::unordered_map<std::string, std::vector<std::string>> meta;
    DnsServiceInfo() = default;
};

struct DnsServiceItem {
    std::string                                        serviceName;
    std::unordered_map<std::string, DnsServiceInfo>    brokers;
    std::chrono::time_point<std::chrono::steady_clock> expiry;
    DnsServiceItem() = default;
    DnsServiceItem(const std::string &service)
        : serviceName(service) {}
};

} // namespace detail

class DnsStorage {
public:
    DnsStorage() = default;

    // Get the singleton instance
    static DnsStorage &getInstance() {
        static DnsStorage instance;
        return instance;
    }

    std::set<std::string>                         _dnsAddress;
    std::map<std::string, detail::DnsServiceItem> _dnsCache;

private:
    // Prevent copy constructor and assignment operator
    DnsStorage(DnsStorage const &) = delete;
    DnsStorage &operator=(DnsStorage const &) = delete;
};

template<units::basic_fixed_string serviceName_, typename... Meta_>
class RequestServiceName {
public:
    explicit RequestServiceName(const Request &In, Reply &Out)
        : In_(In), Out_(Out) {
        processRequest(In_, Out_);
    }

private:
    DnsContext     dnsIn_;
    const Request &In_;
    Reply         &Out_;
    DnsStorage    &storage = DnsStorage::getInstance();
    void           processRequest(const Request &In, Reply &Out) {
        const auto iter = storage._dnsCache.find(In.serviceName);
        if (iter == storage._dnsCache.end()) {
            throw std::invalid_argument(
                              fmt::format("Inavlid Service Name {}", In.serviceName));
        }
        detail::DnsServiceItem Item = iter->second;
        for (auto const &[brokerName, brokerInfo] : Item.brokers) {
            Out.uris.insert(Out.uris.end(), brokerInfo.uris.begin(), brokerInfo.uris.end());
            // Out.meta[serviceInfo.meta.first].push_back(serviceInfo.meta.second);
        }
    }
};

template<units::basic_fixed_string serviceName_, typename... Meta_>
class RequestBrokerName {
public:
    explicit RequestBrokerName(const Request &In, Reply &Out)
        : In_(In), Out_(Out) {
        processRequest(In_, Out_);
    }

private:
    DnsContext     dnsIn_;
    const Request &In_;
    Reply         &Out_;
    DnsStorage    &storage = DnsStorage::getInstance();

    void           processRequest(const Request &In, Reply &Out) {
        auto [iter, inserted] = storage._dnsCache.try_emplace(In.serviceName, detail::DnsServiceItem());
        auto &Item            = iter->second;
        if (Item.brokers.find(In.brokerName) == Item.brokers.end()) {
            throw std::invalid_argument(
                              fmt::format("Inavlid broker Name {}", In.serviceName));
        }
        Out.uris.insert(Out.uris.end(), Item.brokers[In.brokerName].uris.begin(),
                          Item.brokers[In.brokerName].uris.end());
    }
};

template<units::basic_fixed_string serviceName_, typename... Meta_>
class RequestSignalName {
public:
    explicit RequestSignalName(const Request &In, Reply &Out)
        : In_(In), Out_(Out) {
        processRequest(In_, Out_);
    }

private:
    DnsContext     dnsIn_;
    const Request &In_;
    Reply         &Out_;
    DnsStorage    &storage = DnsStorage::getInstance();

    void           processRequest(const Request &In, Reply &Out) {
        auto service = storage._dnsCache.find(In.serviceName);
        if (service != storage._dnsCache.end()) {
            auto broker = service->second.brokers.find(In.brokerName);
            if (broker != service->second.brokers.end()) {
                /*  if(broker->second.signalNames.find(In.signalName) != broker->second.signalNames.end()){
                      broker->second.signalNames.insert(In.signalName);
                  }*/
                // if (signal != broker->second.signalNames.end()) {
                Out.uris.insert(Out.uris.end(), service->second.brokers[In.brokerName].uris.begin(),
                                  service->second.brokers[In.brokerName].uris.end());
                std::string signalName = In.signalName;
                std::transform(Out.uris.begin(), Out.uris.end(), Out.uris.begin(),
                                  [signalName](const std::string &uri) {
                            return uri + "?" + "signal_name" + "=" + signalName;
                                  });
                // Out.meta[broker->second.services[In.serviceName].meta.first].push_back(broker->second.services[In.serviceName].meta.second);
                //}
            }
        }
    }
};

template<units::basic_fixed_string serviceName_, typename... Meta_>
class Dns : public Worker<serviceName_, DnsContext, Request,
                    Reply, Meta_...> {
    using super_t = Worker<serviceName_, DnsContext, Request,
            Reply, Meta_...>;

public:
    const std::string brokerName;
    template<typename BrokerType>
    explicit Dns(const BrokerType &broker)
        : super_t(broker, {}), brokerName(std::move(broker.brokerName)) {
        super_t::setCallback([this](
                                     const RequestContext &rawCtx,
                                     const DnsContext &dnsIn, const Request &In,
                                     DnsContext &dnsOut, Reply &Out) {
            if (rawCtx.request.command() == Command::Get) {
                fmt::print("worker recieved 'get' request\n");
                handleGetRequest(dnsIn, In, Out);
                fmt::print("{}, {}", In.brokerName, In.serviceName);
            } else if (rawCtx.request.command() == Command::Set) {
                fmt::print("worker received 'set' request\n");
                // handleSetRequest(dnsIn, dnsOut, Out);
            }
        });
    }

    void registerDnsAddress(const opencmw::URI<> &address) {
        fmt::print("register dns address get called");
        storage._dnsAddress.emplace(address.str());
        auto [iter, inserted]    = storage._dnsCache.try_emplace(serviceName_.c_str(), detail::DnsServiceItem());
        auto       &Item         = iter->second;
        std::string service_name = serviceName_.c_str();
        Item.brokers.try_emplace(brokerName, detail::DnsServiceInfo());
        Item.brokers[brokerName].uris.insert(address.str() + "/" + service_name);
    }

private:
    using RequestBrokerName_t  = RequestBrokerName<serviceName_, Meta_...>;
    using RequestSignalName_t  = RequestSignalName<serviceName_, Meta_...>;
    using RequestServiceName_t = RequestServiceName<serviceName_, Meta_...>;
    DnsStorage &storage        = DnsStorage::getInstance();

    void        handleGetRequest(const DnsContext &dnsIn, const Request &In,
                   Reply &Out) {
        if (!In.serviceName.empty() && In.brokerName.empty() && In.signalName.empty()) {
            RequestServiceName_t worker(In, Out);
        } else if (!In.serviceName.empty() && !In.brokerName.empty() && In.signalName.empty()) {
            RequestBrokerName_t worker(In, Out);
        } else if (!In.signalName.empty() && !In.serviceName.empty() && !In.brokerName.empty()) {
            RequestSignalName_t worker(In, Out);
        }
    }
};
} // namespace opencmw::DNS
#endif