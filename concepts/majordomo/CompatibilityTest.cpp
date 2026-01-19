/*
 * A simple test binary to test interoperability between different opencmw implementations
 * - Print serialised domain objects to check serialiser identity
 * - Start a majordomo broker with multiple endpoints:
 *   - `testProperty`: a service which provides an unannotated Property containing all
 *   - `annotatedProperty`: the same but with metadata
 *   - `OpencmwCPP/Acquisition`: a simple acquisition object which is periodically notified
 *   - `GnuRadio/Acquisition`: mock version of the opendigitizer acquisition property for streaming and dataset data
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

Acquisition createAcqObj() {
    Acquisition out{};
    out.channelValues = opencmw::MultiArray<float, 2>{ { 1.f, 2.f, 3.f }, { 1UZ, 3UZ } };
    return out;
}

struct TimeDomainContext {
    std::string ctx;
    std::string channelNameFilter;
    std::string acquisitionModeFilter = "Triggered"; // one of "Continuous", "Triggered", "Multiplexed", "Snapshot"
    std::string triggerNameFilter;
    int32_t     maxClientUpdateFrequencyFilter = 25;
    int32_t                 preSamples        = 0;                     // Trigger mode
    int32_t                 postSamples       = 0;                     // Trigger mode
    int32_t                 maximumWindowSize = 65535;                 // Multiplexed mode
    int64_t                 snapshotDelay     = 0;                     // nanoseconds, Snapshot mode
    opencmw::MIME::MimeType contentType       = opencmw::MIME::BINARY; // YaS
};
ENABLE_REFLECTION_FOR(TimeDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, preSamples, postSamples, maximumWindowSize, snapshotDelay, contentType)

// mocks the gr4 tag but with a few fixed fields instead of a generic map-like type
struct Tag {
    std::size_t   idx = 0;
    std::uint64_t trigger_time = 0;
    std::string   trigger_name;
    float         offset = 0;
};

template<units::basic_fixed_string serviceName, typename... Meta>
class GnuRadioAcquisitionWorker : public majordomo::Worker<serviceName, TimeDomainContext, majordomo::Empty, Acquisition, Meta...> {
    std::jthread                                  _notifyThread;

public:
    using super_t = majordomo::Worker<serviceName, TimeDomainContext, majordomo::Empty, Acquisition, Meta...>;

    explicit GnuRadioAcquisitionWorker(opencmw::URI<> brokerAddress, const zmq::Context& context, const std::chrono::milliseconds rate, majordomo::Settings settings = {}) : super_t(std::move(brokerAddress), {}, context, std::move(settings)) {
        init(rate);
    }

    template<typename BrokerType>
    explicit GnuRadioAcquisitionWorker(BrokerType& broker, std::chrono::milliseconds rate) : super_t(broker, {}) {
        // this makes sure the subscriptions are filtered correctly
        opencmw::query::registerTypes(TimeDomainContext(), broker);
        init(rate);
    }

    ~GnuRadioAcquisitionWorker() {
        _notifyThread.request_stop();
        _notifyThread.join();
    }

    enum class AcquisitionMode { Continuous, Triggered, Multiplexed, Snapshot, DataSet };
    static AcquisitionMode AcquisitionModeFromString(std::string_view string) {
        auto case_insensitive_compare = [](auto &l, auto &r) {return std::tolower(l) == std::tolower(r);};
        if (std::ranges::equal(string, "Continuous"sv, case_insensitive_compare)) {
            return AcquisitionMode::Continuous;
        } else if (std::ranges::equal(string, "Triggered"sv, case_insensitive_compare)) {
            return AcquisitionMode::Triggered;
        } else if (std::ranges::equal(string, "Multiplexed"sv, case_insensitive_compare)) {
            return AcquisitionMode::Multiplexed;
        } else if (std::ranges::equal(string, "Snapshot"sv, case_insensitive_compare)) {
            return AcquisitionMode::Snapshot;
        } else if (std::ranges::equal(string, "Dataset"sv, case_insensitive_compare)) {
            return AcquisitionMode::DataSet;
        }
        throw std::invalid_argument(std::format("Unknown acquisition mode '{}'", string));
    }

private:
    void init(std::chrono::milliseconds rate) {
        _notifyThread = std::jthread([this, rate](const std::stop_token& stoken) {
            auto last = std::chrono::system_clock::now() - rate;
            while (!stoken.stop_requested()) {
                auto now = std::chrono::system_clock::now();
                if (now < last + rate) {
                    std::this_thread::sleep_for(last + rate - now);
                }
                handleSubscriptions(now.time_since_epoch().count(), last.time_since_epoch().count());
                last = now;
            }
        });
    }

    void handleSubscriptions(std::int64_t t, std::int64_t t_old) {
        std::println("updating MockOpendigitizer notification, {} subscriptions: {}", super_t::activeSubscriptions().size(), super_t::activeSubscriptions() | std::views::transform([](const mdp::Topic &sub) -> std::string { return sub.toZmqTopic(); }));
        for (const auto& subscription : super_t::activeSubscriptions()) {
            const auto filterIn = opencmw::query::deserialise<TimeDomainContext>(subscription.params());
            try {
                const auto acquisitionMode = AcquisitionModeFromString(filterIn.acquisitionModeFilter);
                for (std::string_view signalName : filterIn.channelNameFilter | std::ranges::views::split(',') | std::ranges::views::transform([](const auto&& r) { return std::string_view{&*r.begin(), static_cast<std::size_t>(std::ranges::distance(r))}; })) {
                    if (acquisitionMode == AcquisitionMode::Continuous) {
                        handleStreamingSubscription(filterIn, signalName, t, t_old);
                    } else {
                        handleDataSetSubscription(filterIn, signalName, t, t_old);
                    }
                }
            } catch (const std::exception& e) {
                std::println(std::cerr, "Could not handle subscription {}: {}", subscription.toZmqTopic(), e.what());
            }
        }
    }

    void handleStreamingSubscription(const TimeDomainContext& context, std::string_view signalName, std::int64_t t, std::int64_t t_old) {
        Acquisition reply;

        constexpr float sample_rate = 1.0e5f;
        auto nSamples = static_cast<std::size_t>(static_cast<float>(t - t_old) * 1e-9f * sample_rate);
        float f_sig = 20.0f;

        const std::vector<float> xdata = std::views::iota(0UZ, nSamples)
                | std::views::transform([t_old, sample_rate](const auto i) -> float { return static_cast<float>(t_old) * 1e-9f + static_cast<float>(i) * sample_rate; })
                | std::ranges::to<std::vector>();
        const std::vector<float> data = xdata | std::views::transform([f_sig](const auto time) -> float { return std::sin(time * 2.0f * std::numbers::pi_v<float> * f_sig); }) | std::ranges::to<std::vector>();
        std::vector tags{
            Tag{.idx = 5, .trigger_time = static_cast<std::uint64_t>(xdata[5] * 1e9f), .trigger_name = "EVT_TEST1", .offset = 0.f},
            Tag{.idx = xdata.size() - 10, .trigger_time = static_cast<std::uint64_t>(xdata[xdata.size() - 10] * 1e9f), .trigger_name = "EVT_TEST2", .offset = 0.f},
        };
        std::vector<std::string> errors = {}; // pollerEntry.populateFromTags(tags);
        reply.refTriggerName            = "STREAMING";
        reply.channelNames              = { std::string(signalName) };
        reply.channelUnits              = { "V"s };
        reply.channelQuantities         = { "voltage"s };
        reply.channelRangeMin           = { std::numeric_limits<float>::lowest() };
        reply.channelRangeMax           = { std::numeric_limits<float>::max() };

        const std::array dims{1U, static_cast<std::uint32_t>(nSamples)}; // 1 signal, N samples
        reply.channelValues = opencmw::MultiArray<float, 2>(data, dims);
        reply.channelErrors = opencmw::MultiArray<float, 2>(std::vector(nSamples, 0.f), dims);
        reply.channelTimeSinceRefTrigger = xdata;

        // preallocate trigger vectors to the number of tags
        reply.triggerIndices.reserve(tags.size());
        reply.triggerEventNames.reserve(tags.size());
        reply.triggerTimestamps.reserve(tags.size());
        reply.triggerOffsets.reserve(tags.size());
        reply.triggerYamlPropertyMaps.reserve(tags.size());
        for (auto&[idx, trigger_time, trigger_name, offset] : tags) {
            if (!trigger_name.empty() && trigger_time != 0) {
                const auto  sample_offset = static_cast<std::int64_t>(static_cast<float>(idx) * 1e-9f / sample_rate);
                if (reply.acqLocalTimeStamp == 0) { // just take the value of the first tag. probably should correct for the tag index times samplerate
                    reply.acqLocalTimeStamp = static_cast<std::int64_t>(trigger_time) - sample_offset;
                }
                if (reply.refTriggerStamp == 0) { // just take the value of the first tag. probably should correct for the tag index times samplerate
                    reply.refTriggerName  = trigger_name;
                    reply.refTriggerStamp = static_cast<std::int64_t>(trigger_time);
                }
            }
            reply.triggerIndices.push_back(static_cast<std::int64_t>(idx));
            reply.triggerEventNames.push_back(trigger_name);
            reply.triggerTimestamps.push_back(static_cast<std::int64_t>(trigger_time));
            reply.triggerOffsets.push_back(offset);
            reply.triggerYamlPropertyMaps.push_back(std::format("- trigger_name: {}\n  trigger_time: {}\n  trigger_offest: {}", trigger_name, trigger_time, offset)); // poor man's yaml
        }
        reply.triggerIndices.shrink_to_fit();
        reply.triggerEventNames.shrink_to_fit();
        reply.triggerTimestamps.shrink_to_fit();
        reply.triggerOffsets.shrink_to_fit();
        reply.triggerYamlPropertyMaps.shrink_to_fit();
        if (!errors.empty()) {
            reply.acqErrors = std::move(errors);
        }
        super_t::notify(context, reply);
    }

    void handleDataSetSubscription(const TimeDomainContext& context, std::string_view signalName_, std::int64_t t , std::int64_t t_old) {
        const std::string signalName(signalName_);

        Acquisition reply;
        constexpr float sample_rate = 1.0e4f;
        constexpr float t_rise = 1e-2f;
        constexpr float t0 = 0.5f;
        constexpr float y0 = 0.5f;
        constexpr float y1 = 5.5f;
        constexpr std::size_t nPre = 2000UZ;
        constexpr std::size_t nPost = 8000UZ;
        constexpr auto nSamples = nPre + nPost;
        const auto fn = [t_rise, t0, y0, y1](const float time) { return y0 + y1-y0 / (1 + std::exp(-(4/t_rise)* (time - t0 - t_rise / 2.0f))); };

        const std::vector<uint64_t> xdata = std::views::iota(0UZ, nSamples)
                | std::views::transform([t_old, sample_rate](const auto i) -> uint64_t { return static_cast<uint64_t>(t_old) + static_cast<uint64_t>(static_cast<float>(i) * sample_rate); })
                | std::ranges::to<std::vector>();
        const std::vector<float> data1 = xdata | std::views::transform([&fn](const auto time) -> float { return fn(time); }) | std::ranges::to<std::vector>();
        const std::vector<std::vector<float>> data{{data1}};
        std::vector<Tag> timing_events{};
        std::vector<std::string> errors{};
        std::vector<std::string> warnings{};
        std::vector<std::string> signal_names{"test"};
        std::vector<std::string> signal_units{"V"};
        std::vector<std::string> signal_quantities{"voltage"};

        if (!timing_events.empty()) {
            const auto [triggerName, triggerTime] = std::pair{timing_events[0].trigger_name, timing_events[0].trigger_time}; //detail::findTrigger(timing_events[0]);
            reply.refTriggerName                  = triggerName;
            reply.refTriggerStamp                 = static_cast<std::int64_t>(triggerTime);
        }

        const std::size_t nSignals = static_cast<uint32_t>(data.size());

        reply.channelNames.clear();
        reply.channelNames.reserve(nSignals);
        reply.channelQuantities.clear();
        reply.channelQuantities.reserve(nSignals);
        reply.channelUnits.clear();
        reply.channelUnits.reserve(nSignals);
        reply.channelRangeMin.clear();
        reply.channelRangeMin.reserve(nSignals);
        reply.channelRangeMax.clear();
        reply.channelRangeMax.reserve(nSignals);
        reply.status.clear();
        reply.status.reserve(nSignals);
        reply.temperature.clear();
        reply.temperature.reserve(nSignals);

        for (std::size_t i = 0; i < nSignals; i++) {
            reply.channelNames.push_back(std::string(signal_names[i]));
            reply.channelQuantities.push_back(std::string(signal_quantities[i]));
            reply.channelUnits.push_back(std::string(signal_units[i]));

            reply.channelRangeMin.push_back(std::numeric_limits<float>::lowest());
            reply.channelRangeMax.push_back(std::numeric_limits<float>::max());
        }
        // MultiArray stores internally elements as stride 1D array: <values_signal_1><values_signal_2><values_signal_3>
        std::vector<float> values;
        values.reserve(nSignals * nSamples);
        for (uint32_t i = 0; i < nSignals; ++i) {
            auto span = data[i];
            values.insert(values.end(), span.begin(), span.end());
        }
        reply.channelValues = opencmw::MultiArray<float, 2>(std::move(values), std::array{static_cast<uint32_t>(nSignals), static_cast<uint32_t>(nSamples)});

        reply.channelTimeSinceRefTrigger = { static_cast<float>(xdata[0]) };

        if (!timing_events.empty()) {
            const auto& tags = timing_events;
            reply.triggerIndices.reserve(tags.size());
            reply.triggerEventNames.reserve(tags.size());
            reply.triggerTimestamps.reserve(tags.size());
            reply.triggerOffsets.reserve(tags.size());
            reply.triggerYamlPropertyMaps.reserve(tags.size());
            for (auto& [idx, trigger_time, trigger_name, offset] : tags) {
                reply.triggerIndices.push_back(static_cast<int64_t>(idx));
                reply.triggerEventNames.push_back(trigger_name);
                reply.triggerTimestamps.push_back(static_cast<std::int64_t>(trigger_time));
                reply.triggerOffsets.push_back(offset);
                reply.triggerYamlPropertyMaps.push_back(std::format("- trigger_idx: {}\n  trigger_name: {}\n  trigger_time: {}\n  trigger_offest: {}", idx, trigger_name, trigger_time, offset)); // poor man's yaml
            }
            reply.triggerIndices.shrink_to_fit();
            reply.triggerEventNames.shrink_to_fit();
            reply.triggerTimestamps.shrink_to_fit();
            reply.triggerOffsets.shrink_to_fit();
            reply.triggerYamlPropertyMaps.shrink_to_fit();
        }
        super_t::notify(context, reply);
    }
};

/***
 * Data-structure which contains all types supported by the opencmw serialiser.
 * Used to test back-and-forth compatibility.
 */
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
    // std::unique_ptr<TestData> nestedData; // TODO support for nested datastructures
};
ENABLE_REFLECTION_FOR(TestDataAnnotated, booleanValue, int8Value, int16Value, int32Value, int64Value, floatValue, doubleValue, charValue, stringValue, boolArray, int8Array, int16Array, int32Array, int64Array, floatArray, doubleArray, charArray, stringArray /*, nestedData*/)

/***
 * Data-structure which contains all types supported by the opencmw serialiser.
 * Version without metadata annotations. There is at the moment no other way to disable annotations in the Worker API.
 * Used to test back-and-forth compatibility.
 */
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

/***
 * Helper utility that can initialise both annotated and non-annotated TestData objects with the same data.
 */
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

/***
 * simple context type
 */
struct TestContext {
    std::string             contextA{};
    std::string             contextB{};
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};
ENABLE_REFLECTION_FOR(TestContext, /*contextA, contextB,*/ contentType)

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
    using namespace opencmw::majordomo;
    // setup primary broker
    Broker primaryBroker("/PrimaryBroker", testSettings());
    opencmw::query::registerTypes(SimpleContext(), primaryBroker);
    const auto brokerRouterAddress    = primaryBroker.bind(URI<>(std::format("mdp://127.0.0.1:{}", port)));
    const auto brokerRouterAddressPub = primaryBroker.bind(URI<>("mds://127.0.0.1:12346"));
    if (!brokerRouterAddress || !brokerRouterAddressPub) {
        std::println("Could not bind to broker addresses, check if utility is already running or if ports are already used");
        return 1;
    }
    std::println("Broker router addresses: {}, {}", brokerRouterAddress->str(), brokerRouterAddressPub->str());
    // note: our thread handling is very verbose, offer nicer API
    std::jthread primaryBrokerThread([&primaryBroker] {
        primaryBroker.run();
    });

    // simple worker to publish events with a test property
    Worker<"/testProperty", TestContext, Empty, TestData, description<"simple worker to publish events with a test property">> helloWorldWorker(primaryBroker,
            [&](RequestContext &/*rawCtx*/, const TestContext &/*requestContext*/, const Empty &/*in*/, TestContext &/*replyContext*/, TestData &out) {
                std::println("testProperty got a get call");
                out = create_test_data_entry<TestData>(value, n);
            });
    RunInThread                                                                                                                                            runHelloWorld(helloWorldWorker);

    // simple worker to publish events with a test property with annotations
    Worker<"/annotatedProperty", TestContext, Empty, TestDataAnnotated, description<"simple worker to publish events with a test property with annotations">> helloWorldWorkerAnnotated(primaryBroker,
            [&](RequestContext & /*rawCtx*/, const TestContext & /*requestContext*/, const Empty & /*in*/, TestContext & /*replyContext*/, TestDataAnnotated &out) {
                std::println("annotatedProperty got a get call");
                out = create_test_data_entry<TestDataAnnotated>(value, n);
            });
    RunInThread                                                                                                                                            runHelloWorldAnnotated(helloWorldWorkerAnnotated);

    // simple worker to publish events with an acquistion property
    Worker<"/OpencmwCPP/Acquisition", TimeDomainContext, Empty, Acquisition, description<"A friendly service providing acquisition object updates">> acquisitionWorker(primaryBroker,
            [](RequestContext & /*rawCtx*/, const TimeDomainContext & /*requestContext*/, const Empty & /*in*/, TimeDomainContext & /*replyContext*/, Acquisition &out) {
                std::println("AcquisitionProperty got a get call");
                out.channelValues = opencmw::MultiArray<float, 2>{ { 1.f, 2.f, 3.f }, { 1UZ, 3UZ } };
            });
    RunInThread                                                                                                                                            acquisitionWorkerThread(acquisitionWorker);

    GnuRadioAcquisitionWorker<"/GnuRadio/Acquisition"> opendigitizerWorker{primaryBroker, 500ms};
    RunInThread                                           openDigitizerWorkerThread(opendigitizerWorker);

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
            .ctx = "",
            .channelNameFilter = "chanA",
            .acquisitionModeFilter = "triggered",
            .triggerNameFilter = "INJECTION",
            .contentType = opencmw::MIME::BINARY,
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
                annotations = std::string_view(argv[i + 1]) == "true";
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
