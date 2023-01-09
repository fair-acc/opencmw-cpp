#ifndef OPENCMW_CPP_DNS_H
#define OPENCMW_CPP_DNS_H

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <fmt/core.h>

#include "URI.hpp"

#include <opencmw.hpp>

#include <IoSerialiserJson.hpp>

#include <majordomo/Constants.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/SubscriptionMatcher.hpp>
#include <majordomo/Utils.hpp>
#include <majordomo/ZmqPtr.hpp>
#include <QuerySerialiser.hpp>

using namespace std::string_literals;
using namespace opencmw::majordomo;
using namespace std::chrono_literals;

struct Request {
    std::string brokerName;
    std::string address;
    std::string service;
    std::string signalName;
};
ENABLE_REFLECTION_FOR(Request, brokerName, address, service)

struct Reply {
    std::vector<std::string> signalNames;
    std::vector<float> signalValues;
    std::string address;
    std::set<URI<RELAXED>> uris;
    std::chrono::time_point<std::chrono::steady_clock> expiry;
};
ENABLE_REFLECTION_FOR(Reply, signalNames, signalValues, address, uris, expiry)

struct DnsContext {
    opencmw::TimingCtx ctx;
    std::string signalFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::JSON;
};
ENABLE_REFLECTION_FOR(DnsContext, signalFilter, contentType)

namespace opencmw::Dns {
    using BrokerMessage = BasicMdpMessage<MessageFormat::WithSourceId>;

    namespace detail {
        struct DnsServiceItem {
            std::optional<std::string> address;
            std::optional<std::string> serviceName;
            std::optional<std::string> signalName;
            std::set<URI<RELAXED>> uris;
            std::chrono::time_point<std::chrono::steady_clock> expiry;

            explicit DnsServiceItem(std::string address_, std::string serviceName_)
              : address { std::move(address_) }
              , serviceName { std::move(serviceName_) } {}
        };

   /*inline std::string findDnsEntry(std::string_view brokerName, std::unordered_map<std::string, detail::DnsServiceItem> &dnsCache, std::string_view s) {
        const auto query = URI<RELAXED>(std::string(s));

        const auto queryScheme = query.scheme();
        const auto queryPath = query.path().value_or("");
        const auto strippedQueryPath = stripStart(queryPath, "/");
        const stripStartFromSearchPath = strippedQueryPath.starts_with("dns.") ? fmt::format("/{}", brokerName) : "/";

        const auto entryMatches = [&queryScheme, &strippedQueryPath, &stripStartFromSearchPath](const auto &dnsEntry) {
            if (queryScheme && !iequal(dnsEntry.scheme().value_or(""), *queryScheme)) {
                return false;
            }

            const auto entryPath = dnsEntry.path().value_or("");
            return stripStart(entryPath, stripStartFromSearchPath).starts_with(strippedQueryPath);
        };

        std::vector<std::string> results;
        for (const auto &[cachedQuery, cacheEntry] : dnsCache) {
            using namespace std::views;
            auto matching = cacheEntry.uris | filter(entryMatches) | transform(uriAsString);
            results.insert(results.end(), matching.begin(), matching.end());
        }
    } */
    } // namespace detail

/*
 template<units::basic_fixed_string serviceName, typename... Meta>
 class RequestSignalName : public Worker<serivceName, DnsContext, Request, Reply, Meta...> {
   public: 
      using super_t = Worker<serviceName, DnsContext, Request, Reply, Meta...>; 
      std::unordered_map<std::string, >
 };
 */
 template<units::basic_fixed_string serviceName, typename... Meta>
 class Dns : public Worker<serviceName, DnsContext, Request, Reply, Meta...> ;

 template<units::basic_fixed_string serviceName, typename... Meta>
 class RequestBrokerName : public Worker<serviceName. DnsContext, Request, Reply, Meta...> , public Dns<serviceName, Meta...> ;

 template<units::basic_fixed_string serviceName, typename... Meta>
 class Dns : public Worker<serviceName, DnsContext, Request, Reply, Meta...> {
  
  public: 
    using super_t = Worker<serviceName, DnsContext, Request, Reply, Meta...>;
    std::set<std::string> _dnsAddress;
    std::unordered_map<std::string, detail::DnsServiceItem> _dnsCache;
    using opencmw::majordomo::Broker;
    Broker broker;

    template<typename BrokerType>
    explicit Dns(const BrokerType &broker) 
      : super_t(broker, {}) {
        super_t::setCallback([this](const RequestContext &rawCtx, const DnsContext &dnsIn, Request &In, DnsContext &dnsOut, Reply &Out) {
            if (rawCtx.request.command() == Command::Get) {
                fmt::print("worker recieved 'get' request\n");
                handleGetRequest(dnsIn, In, Out);
            }
            else if (rawCtx.request.command() == Command::Set) {
                fmt::print("worker received 'set' request\n");
                handleSetRequest(dnsIn, dnsOut, Out);
            }
        });
      }

      void registerDnsAddress(const opencmw::URI<> &address) {
        _dnsAddress.emplace(address.str());
        auto [iter, inserted] = _dnsCache.try_emplace(broker.brokerName, std::string(), broker.brokerName);
        iter->second.uris.insert(URI<RELAXED>(address.str()));
        iter->second.address = address.authority();
        iter->second.serviceName = address.path();
        iter->second.signalName = address.queryParam();
        sendDnsHeartBeats(true);
      }

      private: 
        Timestamp _dnsHearBeatAt;
        const Socket _dnsSocket;
         MdpMessage createDnsReadyMessage() {
              auto ready = MdpMessage::createClientMessage(Command::Ready);
              ready.setServiceName(broker.brokerName, Message::dynamic_bytes_tag{});
              ready.setClientRequestId("clientID", Message::static_bytes_tag{});
              ready.setRbacToken(_rbac, MessageFrame::dynamic_bytes_tag{});
              return ready;
         }
         
         void sendDnsHeartBeats(bool force) {
            if(Clock::now() > _dnsHeartBeatAt || force) {
                const auto ready = createDnsReadyMessage();
                for(const auto &dnsAddress : _dnsAddress) {
                    auto toSend = ready.clone();
                    toSend.setTopic(dnsAddress, MessageFrame::dynamic_bytes_tag{});
                    registerWithDnsServices(std::move(toSend));
                }

                for(const auto &[name, service] : _services) {
                    registerNewService(name);
                }
            }
         }

         void registerNewService(std::string_view serivceName) {
            for(const auto &dnsAddress : _dnsAddress) {
                auto ready = createDnsReadyMessage();
                const auto address = fmt::format("{}/{}", dnsAddress, detail::stripStart(serviceName, "/"));
                ready.setTopic(address, MessageFrame::dynamic_bytes_tag{});
                registerWithDnsServices(std::move(ready));
            }
         }

         void registerWithDnsServices(MdpMessage &&readyMessage) {
            auto [it, inserted] = _dnsCache.try_emplace(broker.brokerName, std::string(), broker.brokerName);
            it->second.uris.insert(URI<RELAXED>(std::string(readyMessage.topic())));
            it->second.expiry = updateDnsExpiry();
            readyMessage.send(_dnsSocket).ignoreResult();
         }
        void handleGetRequest(const DnsContext &dnsIn, Request &In, Reply &Out) {
            auto signals = std::string_view(dnsIn.signalFilter) | std::ranges::views::split(',');
            for (const auto &signal : signals) {
                Out.signalNames.emplace_back(std::string_view(signal.begin(), signal.end()));
            }
            std::vector<std::string> results(signals.size());
            if (!In.brokerName.empty()) {
               RequestBrokerName<"/dns.service"> RequestbyBrokerName(broker);
            }
            /*std::transform(signals.begin(), signals.end(), results.begin(), [this](const auto &v) {
                return detail::findDnsEntry(broker.brokerName, _dnsCache, detail::trimmed(v));
            })*/
        }
 };

  template<units::basic_fixed_string serviceName, typename... Meta>
 class RequestBrokerName : public Worker<serviceName, DnsContext, Request, Reply, Meta...> , public Dns<serviceName, Meta...>{
    public: 
     using super_t = Worker<serviceName, DnsContext, Request, Reply, Meta...>;

     template<typename BrokerType> 
     explicit RequestBrokerName(const BrokerType &broker)
       : super_t::setCallback([this](const RequestContext &rawCtx, const DnsContext &dnsIn, Request &In, DnsContext &dnsOut, Reply &Out) {
        if (rawCtx.request.command() == Command::Get) {
            handleGetRequest(dnsIn, In, Out);
        } else if (rawCtx.request.command() == Command::Set) {
            handleSetRequest(dnsIn, dnsOut, Out);
        }
       });

    private: 
      void handleGetRequest(const DnsContext &dnsIn, Request &In, Reply &Out) {
        const auto it = _dnsCache.find(In.brokerName);
        if (it == _dnsCache.end()) {
            throw std::invalid_argument(fmt::format("Inavlid Broker Name {}", In.brokerName));
        }
        Out.uris.insert(it->second.uris);
      }
 };

 template<units::basic_fixed_string serviceName, typename... Meta>
 class RequestSignalName : public Worker<serviceName, DnsContext, Request, Reply, Meta...>, public Dns<serviceName, Meta...>{
    public: 
      using super_t = Worker<serviceName, DnsContext, Request, Reply, Meta...>;

      template<typename BrokerType>
      explicit RequestSignalName(const BrokerType &broker) 
      : super_t::setCallback([this](const RequestContext &rawCtx, const DnsContext &dnsIn, Request &In, DnsContext &dnsOut, Reply &Out){
        if(rawCtx.request.command() == Command::Get) {
            handleGetRequest(dnsIn, In, Out);
        } else if (rawCtx.request.command() == Command::Set) {
            handleSetRequest(dnsIn, dnsOut, Out);
        }
      });

    private: 
     void handleGetRequest(const DnsContext &dnsIn, Request &In, Reply &Out) {
           for(const auto& [name, record] : _dnsCache) {
            if(record.signalName.find(In.signalName) != std::string::npos) {
                Out.signalNames.emplace_back(record.signalName);
            }
           }
     }
 }
} // namespace dns
#endif 