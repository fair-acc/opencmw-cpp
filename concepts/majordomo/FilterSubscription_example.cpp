#include <Client.hpp>
#include <majordomo/base64pp.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Worker.hpp>

#include <atomic>
#include <fstream>
#include <iomanip>
#include <math.h>
#include <ranges>
#include <string_view>
#include <thread>

using namespace opencmw::majordomo;
using namespace std::chrono_literals;

struct FilterContext {
    std::string             signalFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};
ENABLE_REFLECTION_FOR(FilterContext, signalFilter, contentType)

struct Reply {
    std::vector<std::string> signalNames;
    std::vector<float>       signalValues;
};

ENABLE_REFLECTION_FOR(Reply, signalNames, signalValues)

using namespace opencmw::majordomo;

template<units::basic_fixed_string serviceName, typename... Meta>
class AcquisitionWorker : public Worker<serviceName, FilterContext, Empty, Reply> {
    using super_t = Worker<serviceName, FilterContext, Empty, Reply, Meta...>;
    std::mutex                                             mockSignalsLock;
    std::unordered_map<std::string, std::pair<int, float>> mockSignals = { { "A", { 0, 0.0f } }, { "B", { 0, 1.0f } } }; // <signal name, <update inc, value>>

public:
    template<typename BrokerType>
    explicit AcquisitionWorker(const BrokerType &broker)
        : super_t(broker, {}) {
        super_t::setCallback([this](const RequestContext &rawCtx, const FilterContext &filterIn, const Empty & /*in - unused*/, FilterContext &filterOut, Reply &out) {
            if (rawCtx.request.command() == Command::Get) {
                fmt::print("worker received 'get' request\n");
                handleGetRequest(filterIn, filterOut, out);
            } else if (rawCtx.request.command() == Command::Set) {
                fmt::print("worker received 'set' request\n");
                // do some set action
            }
        });
    }

    void updateData() { // some silly updating data
        static int counter      = 0;
        const auto notifySignal = counter % 2 == false ? "A" : "B";
        const auto value        = counter % 2 == false ? sinf(M_2_PIf * static_cast<float>(counter) / 10.0f) : cosf(M_2_PIf * static_cast<float>(counter) / 10.0f);
        if (mockSignals.contains(notifySignal)) {
            fmt::print("updateData({}) - update signal '{}'\n", counter, notifySignal);
            std::lock_guard lockGuard(mockSignalsLock);
            mockSignals[notifySignal].first++;
            mockSignals[notifySignal].second = value;
        } else {
            fmt::print("updateData({}) - updated nothing\n", counter);
        }
        counter++;
        notifyUpdate();
    }

private:
    void handleGetRequest(const FilterContext &filterIn, FilterContext & /*filterOut*/, Reply &out) {
        auto signals = std::string_view(filterIn.signalFilter) | std::ranges::views::split(',');
        for (const auto &signal : signals) {
            out.signalNames.emplace_back(std::string_view(signal.begin(), signal.end()));
        }
        fmt::print("handleGetRequest for '{}'\n", out.signalNames);
        out.signalValues.resize(out.signalNames.size());
        std::lock_guard lockGuard(mockSignalsLock);
        for (std::size_t i = 0; i < out.signalNames.size(); i++) {
            out.signalValues[i] = mockSignals.at(out.signalNames[i]).second;
        }
    }

    bool shallUpdateForTopic(const auto &filterIn) const noexcept {
        auto                               signals = std::string_view(filterIn.signalFilter) | std::ranges::views::split(',');
        std::set<std::string, std::less<>> requestedSignals;
        for (const auto &signal : signals) {
            requestedSignals.emplace(std::string_view(signal.begin(), signal.end()));
        }
        if (requestedSignals.empty()) {
            return false;
        }
        int updateCount = -1;
        for (const auto &signal : requestedSignals) {
            if (!mockSignals.contains(signal)) {
                fmt::print("requested unknown signal '{}'\n", signal);
                return false;
            }
            if (updateCount < 0) {
                updateCount = mockSignals.at(signal).first;
            }
            if (mockSignals.at(signal).first != updateCount || updateCount == 0) {
                // here: don't update if the update count is not identical for all signals
                return false;
            }
        }
        return true;
    }

    void notifyUpdate() {
        for (auto subTopic : super_t::activeSubscriptions()) { // loop over active subscriptions

            const auto          queryMap = subTopic.params();
            const FilterContext filterIn = opencmw::query::deserialise<FilterContext>(queryMap);
            if (!shallUpdateForTopic(filterIn)) {
                break;
            }
            FilterContext filterOut = filterIn;
            Reply         subscriptionReply;
            try {
                handleGetRequest(filterIn, filterOut, subscriptionReply);
                super_t::notify(std::string(serviceName.c_str()), filterOut, subscriptionReply);
            } catch (const std::exception &ex) {
                fmt::print("caught specific exception '{}'\n", ex.what());
            } catch (...) {
                fmt::print("caught unknown generic exception\n");
            }
        }
    }
};

int main() {
    using opencmw::URI;

    // init Broker and one simple Worker
    Broker broker("PrimaryBroker");
    if (!broker.bind(URI<>("mds://127.0.0.1:12345"))) {
        std::cerr << "Could not bind to broker address" << std::endl;
        return 1;
    }
    AcquisitionWorker<"/DeviceName/Acquisition"> acquisitionWorker(broker);

    // start broker and worker as background processes
    std::jthread brokerThread([&broker] { broker.run(); });
    std::jthread workerThread([&acquisitionWorker] { acquisitionWorker.run(); });

    // start some simple subscription client
    fmt::print("starting some client subscriptions\n");
    const Context                                             zctx{};
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(zctx, 20ms, ""));
    opencmw::client::ClientContext client{ std::move(clients) };

    std::atomic<int>               receivedA{ 0 };
    std::atomic<int>               receivedAB{ 0 };
    client.subscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition?signalFilter=A"), [&receivedA](const opencmw::mdp::Message &update) {
        fmt::print("Client('A') received message from service '{}' for endpoint '{}'\n", update.serviceName.str(), update.endpoint.str());
        receivedA++;
    });
    client.subscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition?signalFilter=A,B"), [&receivedAB](const opencmw::mdp::Message &update) {
        fmt::print("Client('A,B') received message from service '{}' for endpoint '{}'\n", update.serviceName.str(), update.endpoint.str());
        receivedAB++;
    });

    std::jthread([&acquisitionWorker] {
        for (int n = 0; n < 10; n++) {
            std::this_thread::sleep_for(100ms);
            acquisitionWorker.updateData();
        }
    }).join();

    fmt::print("received client updates: {} for 'A' and {} for 'A,B'\n", receivedA, receivedAB);
    client.stop();
    // workers terminate when broker shuts down
    brokerThread.join();
    workerThread.join();
}
