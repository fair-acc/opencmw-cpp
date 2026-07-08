#include <Client.hpp>
#include <concepts/majordomo/helpers.hpp>
#include <majordomo/Rbac.hpp>

#include <catch2/catch.hpp>

// execute also at runtime so that it's noticed by gcov
#define STATIC_REQUIRE2(expr) \
    { \
        STATIC_REQUIRE(expr); \
        REQUIRE(expr); \
    }

TEST_CASE("RBAC parser tests", "[rbac][parsing]") {
    using namespace opencmw::majordomo;
    using namespace std::literals;

    STATIC_REQUIRE2(parse_rbac::role("").empty());
    STATIC_REQUIRE2(parse_rbac::hash("").empty());
    STATIC_REQUIRE2(parse_rbac::roleAndHash("") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(parse_rbac::role("RBAC=ADMIN").empty()); // missing comma
    STATIC_REQUIRE2(parse_rbac::hash("RBAC=ADMIN").empty());
    STATIC_REQUIRE2(parse_rbac::roleAndHash("RBAC=ADMIN") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(parse_rbac::role("ADMIN").empty()); // No "RBAC=" prefix
    STATIC_REQUIRE2(parse_rbac::hash("ADMIN").empty());
    STATIC_REQUIRE2(parse_rbac::roleAndHash("ADMIN") == std::pair(""sv, ""sv));
    STATIC_REQUIRE2(parse_rbac::role("RBAC=ADMIN,") == "ADMIN");
    STATIC_REQUIRE2(parse_rbac::hash("RBAC=ADMIN,") == "");
    STATIC_REQUIRE2(parse_rbac::roleAndHash("RBAC=ADMIN,") == std::pair("ADMIN"sv, ""sv));
    STATIC_REQUIRE2(parse_rbac::role("RBAC=ADMIN,hash") == "ADMIN");
    STATIC_REQUIRE2(parse_rbac::hash("RBAC=ADMIN,hash") == "hash");
    STATIC_REQUIRE2(parse_rbac::roleAndHash("RBAC=ADMIN,hash") == std::pair("ADMIN"sv, "hash"sv));
    STATIC_REQUIRE2(parse_rbac::hash("RBAC=ADMIN,hash,invalidHash") == "hash,invalidHash"); // TODO in java, this throws, should we throw/return "", too?
}

namespace opencmw {

struct RbacContext {
    int dummy;
};

struct RbacInput {
    int dummy;
};

struct RbacOutput {
    int i;
};

} // namespace opencmw

ENABLE_REFLECTION_FOR(opencmw::RbacContext, dummy)
ENABLE_REFLECTION_FOR(opencmw::RbacInput, dummy)
ENABLE_REFLECTION_FOR(opencmw::RbacOutput, i)

namespace {
using RbacWorkerType = majordomo::Worker<"/rbac", opencmw::RbacContext, opencmw::RbacInput, opencmw::RbacOutput, majordomo::description<"Rbac test">>;
};

class RbacWorker : public RbacWorkerType {
public:
    template<typename BrokerType>
    explicit RbacWorker(const BrokerType &broker) : RbacWorkerType(broker, {}) { init(); };

private:
    void init() {
        RbacWorkerType::setCallback([this](const majordomo::RequestContext &rawCtx, const opencmw::RbacContext &context, const opencmw::RbacInput &in, opencmw::RbacContext &replyContext, opencmw::RbacOutput &out) {
            out.i = 42;
        });
    };
};

TEST_CASE("RBAC basic predicate tests", "[rbac][predicate]") {
    opencmw::majordomo::Broker<> broker{ "/Broker", {} };
    RbacWorker                   worker{ broker };

    worker.setRbacPredicate([](auto &) { return false; });

    REQUIRE(broker.bind(opencmw::URI<>("mds://127.0.0.1:12345")));
    REQUIRE(broker.bind(opencmw::URI<>("mdp://127.0.0.1:12346")));

    RunInThread brokerThread(broker);
    RunInThread workerThread(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(broker.context, 20ms, ""));
    opencmw::client::ClientContext client{ std::move(clients) };

    opencmw::IoBuffer              inBuf;
    opencmw::RbacInput             in;
    opencmw::serialise<opencmw::YaS>(inBuf, in);

    std::condition_variable cv;
    std::mutex              m;
    bool                    done = false;

    client.set(opencmw::URI("mdp://127.0.0.1:12346/rbac"), [&](const mdp::Message &reply) {
        REQUIRE(reply.error.size());

        worker.setRbacPredicate([](auto& rb){
                return true;});

        opencmw::IoBuffer iBuf;
        opencmw::serialise<opencmw::YaS>(iBuf, in);
        client.set(opencmw::URI("mdp://127.0.0.1:12346/rbac"), [&](const mdp::Message &reply) {
                auto outBuf = reply.data;
                REQUIRE(reply.error.empty());
                REQUIRE(outBuf.size());
                opencmw::RbacOutput out;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(outBuf, out);
                REQUIRE(out.i == 42);

                std::lock_guard lk(m);
                done = true;
                cv.notify_one();
        }, std::move(iBuf)); }, std::move(inBuf));

    std::unique_lock lk(m);
    cv.wait(lk, [&] { return done; });
}

TEST_CASE("RBAC predicate tests", "[rbac][predicate]") {
    opencmw::majordomo::Broker<> broker{ "/Broker", {} };
    RbacWorker                   worker{ broker };

    broker.setRbacHandler([](auto &msg) { msg.rbac.put("RBAC=USER,hash"); });
    worker.setRbacPredicate([](auto &buf) { return buf.asString().contains("ADMIN"); });

    REQUIRE(broker.bind(opencmw::URI<>("mds://127.0.0.1:12345")));
    REQUIRE(broker.bind(opencmw::URI<>("mdp://127.0.0.1:12346")));

    RunInThread brokerThread(broker);
    RunInThread workerThread(worker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, worker));

    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(broker.context, 20ms, ""));
    opencmw::client::ClientContext client{ std::move(clients) };

    opencmw::IoBuffer              inBuf;
    opencmw::RbacInput             in;
    opencmw::serialise<opencmw::YaS>(inBuf, in);

    std::condition_variable cv;
    std::mutex              m;
    bool                    done = false;

    client.set(opencmw::URI("mdp://127.0.0.1:12346/rbac"), [&](const mdp::Message &reply) {
        REQUIRE(reply.error.size());

        broker.setRbacHandler([](auto& msg){msg.rbac.put("RBAC=ADMIN,hash");});

        opencmw::IoBuffer iBuf;
        opencmw::serialise<opencmw::YaS>(iBuf, in);
        client.set(opencmw::URI("mdp://127.0.0.1:12346/rbac"), [&](const mdp::Message &reply) {
                auto outBuf = reply.data;
                REQUIRE_MESSAGE(reply.error.empty(), reply.error);
                REQUIRE(outBuf.size());
                opencmw::RbacOutput out;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(outBuf, out);
                REQUIRE(out.i == 42);

                std::lock_guard lk(m);
                done = true;
                cv.notify_one();
        }, std::move(iBuf)); }, std::move(inBuf));

    std::unique_lock lk(m);
    cv.wait(lk, [&] { return done; });
}
