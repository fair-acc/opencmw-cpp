#ifndef OPENCMW_CPP_DIRECTORYLIGHTCLIENT_HPP
#define OPENCMW_CPP_DIRECTORYLIGHTCLIENT_HPP

#include <latch>
#include <map>
#include <ranges>
#include <string_view>

#include <ThreadPool.hpp>

#include <cpr/cpr.h>

#include <IoSerialiserJson.hpp>

namespace opencmw::client::cmwlight {
    struct NameserverReplyLocation {
        std::string domain;
        std::string endpoint;
    };
    struct NameserverReplyServer {
        std::string name;
        NameserverReplyLocation location;
    };

    struct NameserverReplyResource {
        std::string name;
        NameserverReplyServer server;
    };

    struct NameserverReply {
        std::vector<NameserverReplyResource> resources;
    };
}
ENABLE_REFLECTION_FOR(opencmw::client::cmwlight::NameserverReplyLocation, domain, endpoint)
ENABLE_REFLECTION_FOR(opencmw::client::cmwlight::NameserverReplyServer, name, location)
ENABLE_REFLECTION_FOR(opencmw::client::cmwlight::NameserverReplyResource, name, server)
ENABLE_REFLECTION_FOR(opencmw::client::cmwlight::NameserverReply, resources)

/*
 * Implements the CMW Directory Server lookup
 *
 * TODO:
 * - allow more fields than just the address to be returned
 * - how to communicate nameserver errors to the client
 * - timeouts for queries and TTL for cache
 *
* minimal API usage example
* curl ${CMW_NAMESERVER}/api/v1/devices/search --json '{ "proxyPreferred" : true, "domains" : [ ], "directServers" : [ ], "redirects" : { }, "names" : [ "GSITemplateDevice" ]}'
* {"resources":[{"name":"GSITemplateDevice","server":{"name":"GSITemplate_DU.vmla017","location":{"domain":"RDA3","endpoint":"9#Address:#string#19#tcp:%2F%2Fvmla017:36725#ApplicationId:#string#124#app=GSITemplate_DU;uid=root;host=vmla017;pid=398;os=Linux%2D6%2E6%2E111%2Drt31%2Dyocto%2Dpreempt%2Drt;osArch=64bit;appArch=64bit;lang=C%2B%2B;#Language:#string#3#C%2B%2B#Name:#string#22#GSITemplate_DU%2Evmla017#Pid:#int#398#ProcessName:#string#14#GSITemplate_DU#StartTime:#long#1771235547903#UserName:#string#4#root#Version:#string#5#5%2E1%2E1"}}}]}
*/

namespace opencmw::client::cmwlight {
class DirectoryLightClient {
    std::map<std::string, std::string, std::less<>> cache;
    std::mutex mutex;
    std::deque<std::string> pendingLookups;
    std::string nameserver;

public:
    explicit DirectoryLightClient(std::string _nameserver) : nameserver(std::move(_nameserver)) {}

    std::optional<std::string> lookup(const std::string_view name, const bool useCache = true) {
        {
            std::lock_guard lock{ mutex };
            if (useCache) {
                if (const auto &res = cache.find(name); res != cache.end()) {
                    if (!res->second.empty()) {
                        return res->second;
                    } else {
                        return {}; // request was already sent
                    }
                }
            }
            cache[std::string{name}] = "";
        }
        triggerRequest(name);
        return {};
    }

    void addStaticLookup(const std::string & deviceName, const std::string & address) {
        std::lock_guard lock{ mutex };
        cache[deviceName] = address;
    };

    static std::optional<std::string> parseEndpoint(std::string_view endpoint, const std::string &classname) {
        using namespace std::literals;
        auto urlDecode = [](const std::string &str) {
            std::string ret;
            ret.reserve(str.length());
            const std::size_t len = str.length();
            for (std::size_t i = 0; i < len; i++) {
                if (str[i] != '%') {
                    if (str[i] == '+') {
                        ret += ' ';
                    } else {
                        ret += str[i];
                    }
                } else if (i + 2 < len) {
                    auto toHex = [](char c) {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        throw std::runtime_error("Invalid hexadecimal number");
                    };
                    const char ch = static_cast<char>('\x10' * toHex(str.at(i + 1)) + toHex(str.at(i + 2)));
                    ret += ch;
                    i = i + 2;
                }
            }
            return ret;
        };
        auto tokens = std::views::split(endpoint, "#"sv);
        if (tokens.empty()) {
            return {};
        }
        std::string fieldCountString = { tokens.front().data(), tokens.front().size() };
        char       *end              = to_address(fieldCountString.end());
        std::size_t fieldCount       = std::strtoull(fieldCountString.data(), &end, 10);

        auto        range    = std::views::drop(tokens, 1);
        auto        iterator = range.begin();
        std::size_t n        = 0;
        while (n < fieldCount) {
            std::string_view fieldNameView{ &(*iterator).front(), (*iterator).size() };
            std::string      fieldname{ fieldNameView.substr(0, fieldNameView.size() - 1) };
            ++iterator;
            if (std::string type{ std::string_view{ &(*iterator).front(), (*iterator).size() } }; type == "string") {
                ++iterator;
                std::string sizeString{ std::string_view{ &(*iterator).front(), (*iterator).size() } };
                auto        parsed = std::to_address(sizeString.end());
                std::size_t size   = std::strtoull(sizeString.data(), &parsed, 10);
                ++iterator;
                std::string string{ std::string_view{ &(*iterator).front(), (*iterator).size() } };
                auto value = urlDecode(string);
                if (fieldname == "Address") {
                    return value;;
                }
            } else if (type == "int") {
                ++iterator;
                std::string sizeString{ std::string_view{ &(*iterator).front(), (*iterator).size() } };
                int         number = std::atoi(sizeString.data());
            } else if (type == "long") {
                ++iterator;
                std::string sizeString{ std::string_view{ &(*iterator).front(), (*iterator).size() } };
                auto        parsed = std::to_address(sizeString.end());
                long        number = std::strtol(sizeString.data(), &parsed, 10);
            } else {
                FAIL(std::format("unknown type: {}, field: {}, endpoint: {}", type, fieldname, endpoint));
            }
            ++iterator;
            ++n;
        }
        return {};
    }

    static std::optional<std::pair<std::string, std::string>> parseNameserverReply(const std::string &reply) {
        IoBuffer buffer{reply.data(), reply.size()};
        NameserverReply replyObj;
        auto res = opencmw::deserialise<Json, ProtocolCheck::LENIENT>(buffer, replyObj);
        for (const auto &[name, server] : replyObj.resources) {
            if (server.location.domain != "RDA3") {
                continue;
            }
            if (auto address = parseEndpoint(server.location.endpoint, name)) {
                return std::optional(std::make_pair(name, address.value()));
            }
        }
        return {};
    }

    void triggerRequest(std::string_view name) {
        using namespace std::literals;
        std::string requestData = std::format(R"""({{ "proxyPreferred" : true, "domains" : [ ], "directServers" : [ ], "redirects" : {{ }}, "names" : [ "{}" ]}})""", name);
        const auto uri = URI<>::factory(URI(std::string(nameserver))).path("/api/v1/devices/search").build();
        std::string deviceName{name};
        cpr::PostCallback([this, deviceName](cpr::Response response) -> void {
            if (response.status_code == 200) {
                if (std::optional<std::pair<std::string, std::string>> result = parseNameserverReply(response.text); result) {
                    if (deviceName != result->first) {
                        throw std::runtime_error("unexpected device name in reply");
                    }
                    std::lock_guard lock{ mutex };
                    cache[std::string{result->first}] = result->second;
                }
            }
        }, cpr::Url{uri.str()}, cpr::Body{requestData}, cpr::Header{{"Content-Type", "application/json"}});
    }
};
}
#endif // OPENCMW_CPP_DIRECTORYLIGHTCLIENT_HPP
