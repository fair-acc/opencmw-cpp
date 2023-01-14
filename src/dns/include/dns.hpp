#ifndef OPENCMW_CPP_DNS_H
#define OPENCMW_CPP_DNS_H

#include <fmt/core.h>

#include <Client.hpp>
#include <IoSerialiserJson.hpp>
#include <QuerySerialiser.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <iomanip>
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

#include "URI.hpp"

using namespace std::string_literals;
// using namespace opencmw::majordomo;
using namespace std::chrono_literals;

struct Request {
  std::string brokerName;
  std::string serviceName;
  std::string signalName;
  std::unordered_map<std::string, std::string> meta;
};
ENABLE_REFLECTION_FOR(Request, brokerName, serviceName, signalName, meta)

struct Reply {
  // std::set<opencmw::URI<opencmw::RELAXED>> uris;
  std::set<std::string> uris;
  std::vector<std::unordered_map<std::string, std::string>> meta;
};
ENABLE_REFLECTION_FOR(Reply, uris, meta)

struct DnsContext {
  opencmw::TimingCtx ctx;
  std::string signalFilter;
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
  std::set<std::string> signalNames;
  std::unordered_map<std::string, std::string> meta;
  DnsServiceInfo() = default;
};

struct DnsServiceItem {
  std::string brokerName;
  std::unordered_map<std::string, DnsServiceInfo> services;
  std::chrono::time_point<std::chrono::steady_clock> expiry;
  DnsServiceItem() = default;
  DnsServiceItem(const std::string &broker) : brokerName(broker) {}
};

}  // namespace detail

struct Storage {
  std::set<std::string> _dnsAddress;
  std::map<std::string, detail::DnsServiceItem> _dnsCache;
  std::unordered_map<std::string, std::string>
      m_brokers;  // signalName -> brokerAddress
  std::unordered_map<std::string, std::string>
      m_services;  // serviceName -> brokerAddress
};

template <units::basic_fixed_string serviceName_, typename... Meta_>
class Dns;

template <units::basic_fixed_string serviceName_, typename... Meta_>
class RequestBrokerName;

template <units::basic_fixed_string serviceName_, typename... Meta_>
class RequestSignalName;

template <units::basic_fixed_string serviceName_, typename... Meta_>
class RequestServiceName;

template <units::basic_fixed_string serviceName_, typename... Meta_>
class Dns : public opencmw::majordomo::Worker<serviceName_, DnsContext, Request,
                                              Reply, Meta_...> {
  using super_t = opencmw::majordomo::Worker<serviceName_, DnsContext, Request,
                                             Reply, Meta_...>;

 public:
  const std::string brokerName;
  template <typename BrokerType>
  explicit Dns(const BrokerType &broker)
      : super_t(broker, {}), brokerName(std::move(broker.brokerName)) {
    super_t::setCallback([this](
                             const opencmw::majordomo::RequestContext &rawCtx,
                             const DnsContext &dnsIn, const Request &In,
                             DnsContext &dnsOut, Reply &Out) {
      if (rawCtx.request.command() == opencmw::majordomo::Command::Get) {
        fmt::print("worker recieved 'get' request\n");
        handleGetRequest(dnsIn, In, Out);
      } else if (rawCtx.request.command() == opencmw::majordomo::Command::Set) {
        fmt::print("worker received 'set' request\n");
        // handleSetRequest(dnsIn, dnsOut, Out);
      }
    });
  }

  void registerDnsAddress(const opencmw::URI<> &address) {
    access._dnsAddress.emplace(address.str());
    auto [iter, inserted] =
        access._dnsCache.try_emplace(brokerName, detail::DnsServiceItem());
    auto &Item = iter->second;
    std::string service_name = serviceName_.c_str();
    Item.services.try_emplace(service_name, detail::DnsServiceInfo());
    Item.services[service_name].uris.insert(address.str() + "/" + service_name);
  }

 private:
  Storage access;
  using RequestBrokerName_t = RequestBrokerName<serviceName_, Meta_...>;
  using RequestSignalName_t = RequestSignalName<serviceName_, Meta_...>;
  using RequestServiceName_t = RequestServiceName<serviceName_, Meta_...>;

  void handleGetRequest(const DnsContext &dnsIn, const Request &In,
                        Reply &Out) {
    if (!In.brokerName.empty() && In.serviceName.empty() &&
        In.signalName.empty()) {
      RequestBrokerName_t worker(In, Out);
    } else if (!In.serviceName.empty() && !In.brokerName.empty() &&
               In.signalName.empty()) {
      RequestServiceName_t worker(In, Out);
    } else if (!In.signalName.empty() && !In.serviceName.empty() &&
               !In.brokerName.empty()) {
      RequestSignalName_t worker(In, Out);
    }
  }
};

template <units::basic_fixed_string serviceName_, typename... Meta_>
class RequestBrokerName {
 public:
  explicit RequestBrokerName(const Request &In, Reply &Out)
      : In_(In), Out_(Out) {
    processRequest(In_, Out_);
  }

 private:
  DnsContext dnsIn_;
  Request In_;
  Reply Out_;
  Storage access;
  void processRequest(const Request &In, Reply &Out) {
    const auto iter = access._dnsCache.find(In.brokerName);
    if (iter == access._dnsCache.end()) {
      throw std::invalid_argument(
          fmt::format("Inavlid Broker Name {}", In.brokerName));
    }
    detail::DnsServiceItem Item = iter->second;
    for (auto const &[serviceName, serviceInfo] : Item.services) {
      Out.uris.insert(serviceInfo.uris.begin(), serviceInfo.uris.end());
      Out.meta.push_back(serviceInfo.meta);
    }
  }
};

template <units::basic_fixed_string serviceName_, typename... Meta_>
class RequestServiceName {
 public:
  explicit RequestServiceName(const Request &In, Reply &Out)
      : In_(In), Out_(Out) {
    processRequest(In_, Out_);
  }

 private:
  DnsContext dnsIn_;
  Request In_;
  Reply Out_;
  Storage access;

  void processRequest(const Request &In, Reply &Out) {
    auto [iter, inserted] =
        access._dnsCache.try_emplace(In.brokerName, detail::DnsServiceItem());
    //  auto iter = access._dnsCache;
    auto &Item = iter->second;
    if (Item.services.find(In.serviceName) == Item.services.end()) {
      throw std::invalid_argument(
          fmt::format("Inavlid Service Name {}", In.serviceName));
    }
    Out.uris.insert(Item.services[In.serviceName].uris.begin(),
                    Item.services[In.serviceName].uris.end());
  }
};

template <units::basic_fixed_string serviceName_, typename... Meta_>
class RequestSignalName {
 public:
  explicit RequestSignalName(const Request &In, Reply &Out)
      : In_(In), Out_(Out) {
    processRequest(In_, Out_);
  }

 private:
  DnsContext dnsIn_;
  Request In_;
  Reply Out_;
  Storage access;

  void processRequest(const Request &In, Reply &Out) {
    auto broker = access._dnsCache.find(In.brokerName);
    if (broker != access._dnsCache.end()) {
      auto service = broker->second.services.find(In.serviceName);
      if (service != broker->second.services.end()) {
        auto signal = service->second.signalNames.find(In.signalName);
        if (signal != service->second.signalNames.end()) {
          Out.uris.insert(broker->second.services[In.serviceName].uris.begin(),
                          broker->second.services[In.serviceName].uris.end());
          Out.meta.push_back(broker->second.services[In.serviceName].meta);
        }
      }
    }
  }
};
}  // namespace opencmw::DNS
#endif