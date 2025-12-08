/*
 * A simple test server to test interoperability between different opencmw implementations
 */

#include <string>
#include <vector>

#include <MIME.hpp>
#include <MultiArray.hpp>
#include <opencmw.hpp>

#include <units/isq/si/frequency.h>
#include <units/isq/si/time.h>

#include "helpers.hpp"
#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>
#include <thread>

namespace majordomo = opencmw::majordomo;
namespace mdp       = opencmw::mdp;

using opencmw::Annotated;
using namespace units::isq;
using namespace units::isq::si;
using namespace std::literals;

/**
 * Generic time domain data object.
 * Specified in: https://edms.cern.ch/document/1823376/1 EDMS: 1823376 v.1 Section 4.3.1
 * Updated version at: https://gitlab.gsi.de/acc/specs/generic-daq#user-content-expected-data-acquisition-variables
 */
struct Acquisition {
    Annotated<std::string, opencmw::NoUnit, "name of timing event used to align the data, e.g. STREAMING or INJECTION1"> refTriggerName  = { "NO_REF_TRIGGER" }; // specified as ENUM
    Annotated<int64_t, si::time<nanosecond>, "UTC timestamp on which the timing event occurred">                         refTriggerStamp = 0;                    // specified as type WR timestamp
    Annotated<std::vector<float>, si::time<second>, "relative time between the reference trigger and each sample [s]">   channelTimeSinceRefTrigger;
    Annotated<float, si::time<second>, "user-defined delay">                                                             channelUserDelay   = 0.0f;
    Annotated<float, si::time<second>, "actual trigger delay">                                                           channelActualDelay = 0.0f;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "names of the channel/signal">                                  channelNames;
    Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit, "values for each channel/signal">                          channelValues;
    Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit, "r.m.s. errors for each channel/signal">                   channelErrors;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "S.I. units of post-processed signals">                         channelUnits;
    Annotated<std::vector<int64_t>, opencmw::NoUnit, "status bit-mask bits for this channel/signal">                     status;
    Annotated<std::vector<float>, opencmw::NoUnit, "minimum expected value for channel/signal">                          channelRangeMin;
    Annotated<std::vector<float>, opencmw::NoUnit, "maximum expected value for channel/signal">                          channelRangeMax;
    Annotated<std::vector<float>, opencmw::NoUnit, "temperature of the measurement device">                              temperature;

    // Additional fields not mandated by the specification but are present in FESA
    Annotated<std::vector<std::string>, opencmw::NoUnit, "event names">                     acquisitionContextCol_eventName;
    Annotated<std::vector<int>, opencmw::NoUnit, "process indices">                         acquisitionContextCol_processIndex;
    Annotated<std::vector<int>, opencmw::NoUnit, "sequence indices">                        acquisitionContextCol_sequenceIndex;
    Annotated<std::vector<int>, opencmw::NoUnit, "chain indices">                           acquisitionContextCol_chainIndex;
    Annotated<std::vector<int>, opencmw::NoUnit, "event numbers">                           acquisitionContextCol_eventNumber;
    Annotated<std::vector<int>, opencmw::NoUnit, "timing group ids">                        acquisitionContextCol_timingGroupID;
    Annotated<std::vector<std::int64_t>, si::time<nanosecond>, "event timestamps">          acquisitionContextCol_eventStamp;
    Annotated<std::vector<std::int64_t>, si::time<nanosecond>, "process start timestamps">  acquisitionContextCol_processStartStamp;
    Annotated<std::vector<std::int64_t>, si::time<nanosecond>, "sequence start timestamps"> acquisitionContextCol_sequenceStartStamp;
    Annotated<std::vector<std::int64_t>, si::time<nanosecond>, "chain start timestamps">    acquisitionContextCol_chainStartStamp;
    Annotated<std::vector<std::uint8_t>, opencmw::NoUnit, "event flags">                    acquisitionContextCol_eventFlags;
    Annotated<std::vector<std::int16_t>, opencmw::NoUnit, "reserved">                       acquisitionContextCol_reserved;
    Annotated<std::vector<std::int64_t>, opencmw::NoUnit, "event_id_raw">                   acquisitionContextCol_event_id_raw;
    Annotated<std::vector<std::int64_t>, opencmw::NoUnit, "param_raw">                      acquisitionContextCol_param_raw;
    Annotated<int, opencmw::NoUnit, "process index">                                        processIndex       = 0;
    Annotated<int, opencmw::NoUnit, "sequence index">                                       sequenceIndex      = 0;
    Annotated<int, opencmw::NoUnit, "chain index">                                          chainIndex         = 0;
    Annotated<int, opencmw::NoUnit, "event number">                                         eventNumber        = 0;
    Annotated<int, opencmw::NoUnit, "timing group id">                                      timingGroupID      = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "acquisition timestamp">                  acquisitionStamp   = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "event timestamp">                        eventStamp         = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "process start timestamp">                processStartStamp  = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "sequence start timestamp">               sequenceStartStamp = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "chain start timestamp">                  chainStartStamp    = 0;

    // Optional fields not mandated by the specification but useful/necessary to propagate additional metainformation
    Annotated<std::vector<std::string>, opencmw::NoUnit, "S.I. quantities of post-processed signals"> channelQuantities;
    Annotated<int64_t, si::time<nanosecond>, "time-stamp w.r.t. beam-in trigger">                     acqLocalTimeStamp = 0;
    Annotated<std::vector<int64_t>, opencmw::NoUnit, "indices of trigger tags">                       triggerIndices;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "event names of trigger tags">               triggerEventNames;
    Annotated<std::vector<int64_t>, si::time<nanosecond>, "timestamps of trigger tags">               triggerTimestamps;
    Annotated<std::vector<float>, si::time<second>, "sample delay w.r.t. the trigger">                triggerOffsets;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "yaml of Tag's property_map">                triggerYamlPropertyMaps;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "list of error messages for this update">    acqErrors;
};
ENABLE_REFLECTION_FOR(Acquisition, refTriggerName, refTriggerStamp, channelTimeSinceRefTrigger, channelUserDelay, channelActualDelay, channelNames, channelValues, channelErrors, channelQuantities, channelUnits, status, channelRangeMin, channelRangeMax, temperature, acqLocalTimeStamp, triggerIndices, triggerEventNames, triggerTimestamps, triggerOffsets, triggerYamlPropertyMaps, acqErrors)

struct TimeDomainContext {
    std::string channelNameFilter;
    std::string acquisitionModeFilter = "triggered"; // one of "continuous", "triggered", "multiplexed", "snapshot"
    std::string triggerNameFilter;
    int32_t     maxClientUpdateFrequencyFilter = 25;
    // TODO should we use sensible defaults for the following properties?
    // TODO make the following unsigned? (add unsigned support to query serialiser)
    int32_t                 preSamples        = 0;                     // Trigger mode
    int32_t                 postSamples       = 0;                     // Trigger mode
    int32_t                 maximumWindowSize = 65535;                 // Multiplexed mode
    int64_t                 snapshotDelay     = 0;                     // nanoseconds, Snapshot mode
    opencmw::MIME::MimeType contentType       = opencmw::MIME::BINARY; // YaS
};
ENABLE_REFLECTION_FOR(TimeDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, preSamples, postSamples, maximumWindowSize, snapshotDelay, contentType)

struct TestDataAnnotated {
    Annotated<bool, opencmw::NoUnit, "Test Field of type bool">           booleanValue{};
    Annotated<std::int8_t, si::time<second>, "Test Field of type int8">   int8Value{};
    Annotated<std::int16_t, si::time<second>, "Test Field of type int16"> int16Value{};
    Annotated<std::int32_t, opencmw::NoUnit, "Test Field of type int32">  int32Value{};
    Annotated<std::int64_t, opencmw::NoUnit, "Test Field of type int64">  int64Value{};
    Annotated<float, opencmw::NoUnit, "Test Field of type float">         floatValue{};
    Annotated<double, opencmw::NoUnit, "Test Field of type double">       doubleValue{};
    Annotated<char, opencmw::NoUnit, "Test Field of type char">           charValue{};
    Annotated<std::string, opencmw::NoUnit, "Test Field of type string">  stringValue;
    Annotated<std::vector<bool>>                                          boolArray;
    Annotated<std::vector<std::int8_t>>                                   int8Array;
    Annotated<std::vector<std::int16_t>>                                  int16Array;
    Annotated<std::vector<std::int32_t>>                                  int32Array;
    Annotated<std::vector<std::int64_t>>                                  int64Array;
    Annotated<std::vector<float>>                                         floatArray;
    Annotated<std::vector<double>>                                        doubleArray;
    Annotated<std::vector<char>>                                          charArray;
    Annotated<std::vector<std::string>>                                   stringArray;
    // std::unique_ptr<TestData> nestedData;
};
ENABLE_REFLECTION_FOR(TestDataAnnotated, booleanValue, int8Value, int16Value, int32Value, int64Value, floatValue, doubleValue, charValue, stringValue, boolArray, int8Array, int16Array, int32Array, int64Array, floatArray, doubleArray, charArray, stringArray /*, nestedData*/)

struct TestData {
    bool                      booleanValue{};
    std::int8_t               int8Value{};
    std::int16_t              int16Value{};
    std::int32_t              int32Value{};
    std::int64_t              int64Value{};
    float                     floatValue{};
    double                    doubleValue{};
    char                      charValue{};
    std::string               stringValue;
    std::vector<bool>         boolArray;
    std::vector<std::int8_t>  int8Array;
    std::vector<std::int16_t> int16Array;
    std::vector<std::int32_t> int32Array;
    std::vector<std::int64_t> int64Array;
    std::vector<float>        floatArray;
    std::vector<double>       doubleArray;
    std::vector<char>         charArray;
    std::vector<std::string>  stringArray;
    // std::unique_ptr<TestData> nestedData;
};
ENABLE_REFLECTION_FOR(TestData, booleanValue, int8Value, int16Value, int32Value, int64Value, floatValue, doubleValue, charValue, stringValue, boolArray, int8Array, int16Array, int32Array, int64Array, floatArray, doubleArray, charArray, stringArray /*, nestedData*/)

struct TestContext {
    std::string             contextA{};
    std::string             contextB{};
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};
ENABLE_REFLECTION_FOR(TestContext, /*contextA, contextB,*/ contentType)

template<typename T>
T create_test_data_entry(std::size_t i, std::size_t n) {
    return T{
        .booleanValue = i % 2 == 0,
        .int8Value    = static_cast<std::int8_t>(i),
        .int16Value   = static_cast<std::int16_t>(i),
        .int32Value   = static_cast<std::int32_t>(i),
        .int64Value   = static_cast<std::int64_t>(i),
        .floatValue   = static_cast<float>(i),
        .doubleValue  = static_cast<double>(i),
        .charValue    = static_cast<char>(i),
        .stringValue  = std::format("test {}", i),
        .boolArray    = std::views::iota(i, i + n) | std::views::transform([](auto x) { return x % 2 == 0; }) | std::ranges::to<std::vector>(),
        .int8Array = std::views::iota(i, i + n) | std::views::transform([](auto x) { return static_cast<std::int8_t>(x); }) | std::ranges::to<std::vector>(),
        .int16Array = std::views::iota(i, i + n) | std::views::transform([](auto x) { return static_cast<std::int16_t>(x); }) | std::ranges::to<std::vector>(),
        .int32Array = std::views::iota(i, i + n) | std::views::transform([](auto x) { return static_cast<std::int32_t>(x); }) | std::ranges::to<std::vector>(),
        .int64Array = std::views::iota(i, i + n) | std::views::transform([](auto x) { return static_cast<std::int64_t>(x); }) | std::ranges::to<std::vector>(),
        .floatArray = std::views::iota(i, i + n) | std::views::transform([](auto x) { return static_cast<float>(x); }) | std::ranges::to<std::vector>(),
        .doubleArray = std::views::iota(i, i + n) | std::views::transform([](auto x) { return static_cast<double>(x); }) | std::ranges::to<std::vector>(),
        .charArray = std::views::iota(i, i + n) | std::views::transform([](auto x) { return static_cast<char>(x); }) | std::ranges::to<std::vector>(),
        .stringArray = std::views::iota(i, i + n) | std::views::transform([](auto x) { return std::format("test{}", x); }) | std::ranges::to<std::vector>(),
        //.nestedData = std::make_unique<TestData>()
    };
}

Acquisition createAcqObj() {
    Acquisition out{};
    out.channelValues = opencmw::MultiArray<float, 2>{ { 1.f, 2.f, 3.f }, { 1UZ, 3UZ } };
    return out;
}

int serialise(const bool annotations, std::size_t i, std::size_t n) {
    opencmw::IoBuffer buffer;
    if (annotations) {
        const auto data = create_test_data_entry<TestDataAnnotated>(i, n);
        opencmw::serialise<opencmw::YaS>(buffer, data);
    } else {
        const auto data = create_test_data_entry<TestData>(i, n);
        opencmw::serialise<opencmw::YaS>(buffer, data);
    }
    std::println("{}", buffer.asString());
    return 0;
}

int serve(const int port, std::size_t value, std::size_t n) {
    using opencmw::URI;
    // setup primary broker
    majordomo::Broker primaryBroker("/PrimaryBroker", testSettings());
    opencmw::query::registerTypes(SimpleContext(), primaryBroker);
    const auto brokerRouterAddress    = primaryBroker.bind(URI<>(std::format("mdp://127.0.0.1:{}", port)));
    const auto brokerRouterAddressPub = primaryBroker.bind(URI<>("mds://127.0.0.1:12346"));
    if (!brokerRouterAddress) {
        std::cerr << "Could not bind to broker address" << std::endl;
        return 1;
    }
    std::println("Broker router addresses: {}, {}", brokerRouterAddress->str(), brokerRouterAddressPub->str());
    // note: our thread handling is very verbose, offer nicer API
    std::jthread primaryBrokerThread([&primaryBroker] {
        primaryBroker.run();
    });

    // simple worker to publish events with a test property
    majordomo::Worker<"/testProperty", TestContext, opencmw::majordomo::Empty, TestData, majordomo::description<"simple worker to publish events with a test property">> helloWorldWorker(primaryBroker,
            [&](majordomo::RequestContext &rawCtx, const TestContext &requestContext, const majordomo::Empty &in, TestContext &replyContext, TestData &out) {
                std::println("testProperty got a get call");
                out = create_test_data_entry<TestData>(value, n);
            });
    RunInThread                                                                                                                                                          runHelloWorld(helloWorldWorker);

    // simple worker to publish events with a test property with annotations
    majordomo::Worker<"/annotatedProperty", TestContext, opencmw::majordomo::Empty, TestDataAnnotated, majordomo::description<"simple worker to publish events with a test property with annotations">> helloWorldWorkerAnnotated(primaryBroker,
            [&](majordomo::RequestContext & /*rawCtx*/, const TestContext & /*requestContext*/, const majordomo::Empty & /*in*/, TestContext & /*replyContext*/, TestDataAnnotated &out) {
                std::println("annotatedProperty got a get call");
                out = create_test_data_entry<TestDataAnnotated>(value, n);
            });
    RunInThread                                                                                                                                                                                         runHelloWorldAnnotated(helloWorldWorkerAnnotated);

    // simple worker to publish events with an acquistion property
    majordomo::Worker<"/OpencmwCPP/Acquisition", TimeDomainContext, opencmw::majordomo::Empty, Acquisition, majordomo::description<"A friendly service providing acquisition object updates">> acquisitionWorker(primaryBroker,
            [](majordomo::RequestContext & /*rawCtx*/, const TimeDomainContext & /*requestContext*/, const majordomo::Empty & /*in*/, TimeDomainContext & /*replyContext*/, Acquisition &out) {
                std::println("AcquisitionProperty got a get call");
                out.channelValues = opencmw::MultiArray<float, 2>{ { 1.f, 2.f, 3.f }, { 1UZ, 3UZ } };
            });
    RunInThread                                                                                                                                                                                acquisitionWorkerThread(acquisitionWorker);

    waitUntilWorkerServiceAvailable(primaryBroker.context, helloWorldWorker);
    waitUntilWorkerServiceAvailable(primaryBroker.context, helloWorldWorkerAnnotated);
    waitUntilWorkerServiceAvailable(primaryBroker.context, acquisitionWorker);

    for (std::size_t i = 0; true; ++i) {
        auto entry          = create_test_data_entry<TestData>(i, n);
        auto entryAnnotated = create_test_data_entry<TestDataAnnotated>(i, n);

        std::println("updating helloWorld notification, {} subscriptions: {}", helloWorldWorker.activeSubscriptions().size(), helloWorldWorker.activeSubscriptions() | std::views::transform([](const mdp::Topic &sub) -> std::string { return sub.toZmqTopic(); }));
        TestContext context{
            .contextA    = "P1",
            .contextB    = "T2",
            .contentType = opencmw::MIME::BINARY
        };
        helloWorldWorker.notify(context, entry);
        helloWorldWorkerAnnotated.notify(context, entryAnnotated);

        std::println("updating acquisition notification, {} subscriptions: {}", acquisitionWorker.activeSubscriptions().size(), acquisitionWorker.activeSubscriptions() | std::views::transform([](const mdp::Topic &sub) -> std::string { return sub.toZmqTopic(); }));
        Acquisition       acqObj = createAcqObj();
        TimeDomainContext context2{
            .channelNameFilter = "chanA",
            .acquisitionModeFilter = "triggered",
            .triggerNameFilter = "INJECTION",
            .contentType = opencmw::MIME::BINARY
        };
        acquisitionWorker.notify(context2, acqObj);

        std::this_thread::sleep_for(2s);
    }
}

enum class TestMode { SERVE,
    SERIALISE,
    UNINITIALISED,
    HELP };

int print_usage() {
    std::println("Usage");
    std::println("  OpenCMWTestServer serve [--port <port>]");
    std::println("    Serve an opencmw broker with /testProperty, /annotatedProperty and /OpenCPP/Acquisition workers");
    std::println("  OpenCMWTestServer serialise [--annotations true|false] [-value <value to put in data>] [-array-size <size of array>]");
    std::println("    deserialise a test object with all datatypes to compare serialiser outputs");
    return 0;
}

int main(const int argc, char **argv) {
    auto        mode        = TestMode::UNINITIALISED;
    std::size_t value       = 42;
    std::size_t arraySize   = 3;
    int         port        = 12345;
    bool        annotations = false;
    for (int i = 1; i < argc; i++) {
        if (std::string_view(argv[i]) == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[i + 1]);
                ++i;
            } else {
                mode = TestMode::HELP;
            }
        } else if (std::string_view(argv[i]) == "--value") {
            if (i + 1 < argc) {
                value = std::stoul(argv[i + 1]);
                ++i;
            } else {
                mode = TestMode::HELP;
            }
        } else if (std::string_view(argv[i]) == "--array-size") {
            if (i + 1 < argc) {
                arraySize = std::stoul(argv[i + 1]);
                ++i;
            } else {
                mode = TestMode::HELP;
            }
        } else if (std::string_view(argv[i]) == "--annotations") {
            if (i + 1 < argc) {
                annotations = argv[i + 1] == "true";
                ++i;
            } else {
                mode = TestMode::HELP;
            }
        } else if (std::string_view(argv[i]) == "serve") {
            if (mode == TestMode::UNINITIALISED) {
                mode = TestMode::SERVE;
            } else {
                mode = TestMode::HELP;
            }
            break;
        } else if (std::string_view(argv[i]) == "serialise") {
            if (mode == TestMode::UNINITIALISED) {
                mode = TestMode::SERIALISE;
            } else {
                mode = TestMode::HELP;
            }
            break;
        } else {
            mode = TestMode::HELP;
            break;
        }
    }
    switch (mode) {
    case TestMode::SERIALISE:
        return serialise(annotations, value, arraySize);
    case TestMode::SERVE:
    case TestMode::UNINITIALISED:
        return serve(port, value, arraySize);
    case TestMode::HELP:
    default:
        return print_usage();
    }
}
