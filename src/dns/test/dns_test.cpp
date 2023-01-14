#include <fmt/format.h>

#include <catch2/catch.hpp>
#include <charconv>
#include <cstdlib>
#include <dns.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/MockClient.hpp>
#include <majordomo/Utils.hpp>
#include <majordomo/Worker.hpp>
#include <thread>

#include "helpers.hpp"

using namespace opencmw::majordomo;
using namespace opencmw::DNS;
using namespace std::chrono_literals;
using URI = opencmw::URI<>;

TEST_CASE("Test dns", "DNS") {
  using opencmw::DNS::Dns;
  using opencmw::majordomo::Broker;
  using opencmw::majordomo::MdpMessage;

  auto settings = testSettings();
  settings.heartbeatInterval = std::chrono::seconds(1);

  const auto dnsAddress =
      opencmw::URI<opencmw::STRICT>("mdp://127.0.0.1:22345");
  const auto brokerAddress =
      opencmw::URI<opencmw::STRICT>("mdp://127.0.0.1:22346");
  Broker dnsBroker("dnsBroker", settings);
  REQUIRE(dnsBroker.bind(dnsAddress));
  settings.dnsAddress = dnsAddress.str();
  Broker broker("testbroker", settings);
  REQUIRE(broker.bind(brokerAddress));
  Dns<"/DeviceName/Dns"> DnsWorker(broker);
  DnsWorker.registerDnsAddress(opencmw::URI<>("https://127.0.0.1:8080"));
}