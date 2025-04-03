#define CATCH_CONFIG_MAIN // This tells the catch header to generate a main
#define CATCH_CONFIG_ENABLE_BENCHMARKING 1

#include <catch2/catch.hpp>
#include <filesystem>
#include <services/dns.hpp>
#include <string>

#ifndef __EMSCRIPTEN__
#include <Client.hpp>
// Concepts and tests use common types
#include <concepts/majordomo/helpers.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#endif

namespace fs = std::filesystem;

#ifndef __EMSCRIPTEN__
class FileDeleter {
public:
    // make sure to delete datastorage file when finishing
    explicit FileDeleter(std::string_view fileName = "dns_data_storage.yas")
        : filename(fileName) {
        deleteFile();
    }
    ~FileDeleter() {
        deleteFile();
    }
    void deleteFile() const {
        if (std::filesystem::exists(filename)) {
            std::filesystem::remove(filename);
        }
    }
    std::string filename;
};

[[maybe_unused]] FileDeleter dsFd; // delete DataStorage file when finishing

#else
class FileDeleter {
public:
    FileDeleter(std::string filename = "") {}
};
#endif // __EMSCRIPTEN__

using namespace opencmw;
using namespace opencmw::service::dns;
using namespace std::chrono_literals;

Entry entry_a{ "http", "localhost", 8080, "group a", "unknown", "A", "ms", 0.123456f, "" };
Entry entry_b{ "http", "localhost", 8080, "group a", "unknown", "B", "ms", 2.3223f, "" };
Entry entry_c{ "http", "localhost", 8080, "test", "unknown", "C", "ms", 3.333f, "" };

class TestDataStorage : public DataStorage {
public:
    void clear() {
        _entries.clear();
    }

    bool save(const char *path) {
        return saveDataToFile(path);
    }

    bool load(const char *path) {
        return loadDataFromFile(path);
    }

    std::vector<StorageEntry> &entries() {
        return _entries;
    }
    explicit TestDataStorage() {
        clear();
    }
};

class NoSaveDataStorage : public TestDataStorage {
    bool saveDataToFile(const char *filepath) {
        std::cout << "good day sir; i am here, in the save file" << std::endl;
        throw std::logic_error{ "I am supposed to be" };
    }
};

TEST_CASE("type tests", "[DNS]") {
    SECTION("FlatEntryList") {
        std::vector<Entry> entries{ entry_a, entry_b, entry_c };
        FlatEntryList      response{ { entry_a, entry_b, entry_c } };
        auto               newEntries = response.toEntries();
        REQUIRE(newEntries[0] == entry_a);
        REQUIRE(newEntries[1] == entry_b);
        REQUIRE(newEntries[2] == entry_c);
    }
    SECTION("QueryEntry") {
        QueryEntry qa;
        qa.signal_name = "A";
        QueryEntry qb;
        qb.signal_name = "B";
        QueryEntry qh;
        qh.protocol = "http";
        REQUIRE(QueryEntry{} == entry_a); // should match all
        REQUIRE(qa == entry_a);
        REQUIRE(qb != entry_a);
        REQUIRE(qb == entry_b);
        REQUIRE(qh == entry_a);
        REQUIRE(qh == entry_b);
    }
}

TEST_CASE("data storage - create, destroy") {
    {
        std::string filename{ "./dns_data_storage.yas" };
        fs::path    filePath(filename);
        if (fs::exists(filePath)) {
            try {
                fs::perms existing_perms = fs::status(filePath).permissions();
                fs::permissions(filePath, existing_perms | fs::perms::owner_write);
            } catch (const std::filesystem::filesystem_error &ex) {
                std::cerr << "Error: " << ex.what() << std::endl;
            }
        }

        DataStorage ds;
        // Do some work here...
        if (fs::exists(filePath)) {
        } else {
            std::ofstream outputFile(filePath);
            std::cout << "created file" << std::endl;
        }

        // Remove owner's write permission
        try {
            fs::permissions(filePath, fs::status(filePath).permissions() & ~fs::perms::owner_write);
        } catch (const std::filesystem::filesystem_error &ex) {
            std::cerr << "Error: " << ex.what() << std::endl;
        }
        ds.addEntry(entry_a);
    }

    FileDeleter fd;
}

TEST_CASE("data storage - Adding Entries", "[DNS]") {
    TestDataStorage ds;

    REQUIRE(ds.getActiveEntriesCount() == 0);
    ds.addEntry(entry_a);
    REQUIRE(ds.getActiveEntriesCount() == 1);
    ds.addEntry(entry_b);
    REQUIRE(ds.getActiveEntriesCount() == 2);
    auto entries = ds.getEntries();
    REQUIRE(entries[0] == entry_a);
    REQUIRE(entries[1] == entry_b);
}

TEST_CASE("data storage - Querying Entries") {
    TestDataStorage ds;
    ds.addEntry(entry_a);
    ds.addEntry(entry_b);
    ds.addEntry(entry_c);
    REQUIRE(ds.getActiveEntriesCount() == 3);
    QueryEntry qc;
    qc.signal_name = "C";
    QueryEntry qGroupA;
    qGroupA.service_name = "group a";

    auto entries         = ds.queryEntries(qc);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0] == entry_c);

    entries = ds.queryEntries(qGroupA);
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0] == entry_a);
    REQUIRE(entries[1] == entry_b);
}

TEST_CASE("data storage - Renewing Entries") {
    TestDataStorage ds;
    ds.addEntry(entry_a);
    ds.addEntry(entry_b);
    auto now = StorageEntry::clock::now();
    ds.addEntry(entry_a);

    REQUIRE(ds.getActiveEntriesCount() == 2);

    auto entries = ds.entries();
    REQUIRE(entries.size() == 2);
    auto e = std::find(entries.begin(), entries.end(), entry_a);
    REQUIRE(e != entries.end());
    REQUIRE(e->ttl > now);
}

TEST_CASE("data storage - Deleting Entries") {
    TestDataStorage ds;
    ds.addEntry(entry_a);
    ds.addEntry(entry_b);
    ds.deleteEntries(entry_a);

    REQUIRE(ds.getActiveEntriesCount() == 1);

    auto entries = ds.entries();
    REQUIRE(entries.size() == 1);
    auto e = std::find(entries.begin(), entries.end(), entry_a);
    REQUIRE(e == entries.end());

    ds.addEntry(entry_a);
    ds.addEntry(entry_c);
}

#ifndef __EMSCRIPTEN__

TEST_CASE("run services", "[DNS]") {
    FileDeleter                                                 fd;
    majordomo::Broker<>                                         broker{ "/Broker", {} };
    std::string                                                 rootPath{ "./" };
    auto                                                        fs = cmrc::assets::get_filesystem();
    majordomo::RestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest_backend{ broker, fs };
    DnsWorkerType                                               dnsWorker{ broker, {} };

    RunInThread                                                 restThread(rest_backend);
    RunInThread                                                 brokerThread(broker);
    RunInThread                                                 dnsThread(dnsWorker);
}

TEST_CASE("client", "[DNS]") {
    FileDeleter                                                 fd;
    majordomo::Broker<>                                         broker{ "/Broker", {} };
    std::string                                                 rootPath{ "./" };
    auto                                                        fs = cmrc::assets::get_filesystem();
    majordomo::RestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest_backend{ broker, fs };
    DnsWorkerType                                               dnsWorker{ broker, DnsHandler{} };
    broker.bind(URI<>{ "inproc://dns_server" }, majordomo::BindOption::Router);

    RunInThread                                               restThread(rest_backend);
    RunInThread                                               dnsThread(dnsWorker);
    RunInThread                                               brokerThread(broker);

    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<client::MDClientCtx>(broker.context, 20ms, "dnsTestClient"));
    clients.emplace_back(std::make_unique<client::RestClient>(opencmw::client::DefaultContentTypeHeader(MIME::BINARY)));
    client::ClientContext clientContext{ std::move(clients) };

    { // MDP
        DnsClient client{ clientContext, URI<>{ "mdp://dns_server/dns" } };
        client.registerSignal(entry_a);
        auto s = client.querySignals();
        REQUIRE(s.size() == 1);
        REQUIRE(s[0] == entry_a);
    }

    { // HTTP
        DnsClient client{ clientContext, URI<>{ "http://localhost:8080/dns" } };
        auto      services = client.querySignals();
        REQUIRE(services.size() == 1);

        auto ret = client.registerSignal(entry_a);
        REQUIRE(ret.signal_rate == entry_a.signal_rate);
        auto rets = client.registerSignals({ entry_b, entry_c });
        REQUIRE(rets[0] == entry_b);
        REQUIRE(rets[1] == entry_c);

        services = client.querySignals();
        REQUIRE(std::ranges::equal(services, std::vector<Entry>{ entry_a, entry_b, entry_c }));
    }

    clientContext.stop();
}

TEST_CASE("query", "[DNS]") {
    FileDeleter                                                 fd;
    majordomo::Broker<>                                         broker{ "/Broker", {} };
    auto                                                        fs = cmrc::assets::get_filesystem();
    majordomo::RestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest_backend{ broker, fs };
    DnsWorkerType                                               dnsWorker{ broker, DnsHandler{} };

    RunInThread                                                 restThread(rest_backend);
    RunInThread                                                 brokerThread(broker);
    RunInThread                                                 dnsThread(dnsWorker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, dnsWorker));

    SECTION("query") {
        auto services = querySignals();
        REQUIRE(services.empty());
        registerSignals({ entry_a });
        services = querySignals();
        REQUIRE(services.size() == 1);
        REQUIRE(services.at(0) == entry_a);
        registerSignals({ entry_b });
        registerSignals({ entry_c });
        services = querySignals();

        REQUIRE(3 == services.size());
        REQUIRE(std::ranges::equal(services, std::vector<Entry>{ entry_a, entry_b, entry_c }));
    }

    SECTION("query with filters") {
        QueryEntry qc;
        qc.signal_name = "C";
        auto services  = querySignals(qc);
        REQUIRE(services.empty());
        registerSignals({ entry_c });
        services = querySignals(qc);
        REQUIRE(services.size() == 1);
        REQUIRE(services[0] == entry_c);
    }
}

TEST_CASE("client unregister entries", "[DNS]") {
    FileDeleter                                                 fd;
    majordomo::Broker<>                                         broker{ "/Broker", {} };
    auto                                                        fs = cmrc::assets::get_filesystem();
    majordomo::RestBackend<majordomo::PLAIN_HTTP, decltype(fs)> rest_backend{ broker, fs };
    DnsWorkerType                                               dnsWorker{ broker, DnsHandler{} };

    RunInThread                                                 restThread(rest_backend);
    RunInThread                                                 brokerThread(broker);
    RunInThread                                                 dnsThread(dnsWorker);

    REQUIRE(waitUntilWorkerServiceAvailable(broker.context, dnsWorker));

    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<client::MDClientCtx>(broker.context, 20ms, "dnsTestClient"));
    clients.emplace_back(std::make_unique<client::RestClient>(opencmw::client::DefaultContentTypeHeader(MIME::BINARY)));
    client::ClientContext clientContext{ std::move(clients) };
    DnsClient             restClient{ clientContext, URI<>{ "http://localhost:8080/dns" } };
    restClient.registerSignals({ entry_a, entry_b, entry_c });

    SECTION("unregister") {
        auto deleted = restClient.unregisterSignal(entry_a);
        REQUIRE(deleted.size() == 1);
        REQUIRE(deleted.at(0) == entry_a);
        auto remaining = restClient.querySignals();
        REQUIRE(remaining.size() == 2);
        REQUIRE(std::ranges::none_of(remaining, [](auto &e) { return e == entry_a; }));
    }
    SECTION("unregister entries") {
        auto res = restClient.unregisterSignals({ entry_a, entry_b });
        REQUIRE(res.size() == 2);

        auto rem = restClient.querySignals();
        REQUIRE(rem.size() == 1);
        REQUIRE(rem.at(0) == entry_c);
    }
    SECTION("unregister group") {
        REQUIRE(restClient.querySignals().size() == 3);
        QueryEntry e;
        e.service_name = "group a";
        auto res       = restClient.unregisterSignal(e);
        REQUIRE(res.size() == 2);
        auto remaining = restClient.querySignals();
        REQUIRE(remaining.size() == 1);
        REQUIRE(remaining.at(0) == entry_c);
    }
}

#endif // __EMSCRIPTEN__

TEST_CASE("registering", "[DNS]") {
    SECTION("registering service") {
        TestDataStorage ds;

        ds.addEntry(entry_a);
        REQUIRE(ds.queryEntries().size() == 1);
        REQUIRE(ds.queryEntries()[0] == entry_a);
    }
    SECTION("unregistering service when not reregistered") {
        TestDataStorage ds;
        ds.addEntry(entry_a);
        ds.addEntry(entry_b);
        ds.addEntry(entry_c);

        auto &entries      = ds.entries();

        entries[0].ttl     = std::chrono::system_clock::now();
        entries[2].ttl     = std::chrono::system_clock::now();

        auto activeEntries = ds.queryEntries();
        REQUIRE(activeEntries.size() == 1);
        REQUIRE(activeEntries[0] == entry_b);
    }

    SECTION("reregistering service") {
        TestDataStorage ds;
        ds.addEntry(entry_a);
        ds.addEntry(entry_b);
        ds.addEntry(entry_c);
        auto &entries  = ds.entries();

        entries[0].ttl = std::chrono::system_clock::now();
        entries[2].ttl = std::chrono::system_clock::now();
        auto ttl       = entries[2].ttl;
        auto reEntry   = entries[2];
#if (defined __EMSCRIPTEN__) || defined(__clang__)
        std::this_thread::sleep_for(0.5s); // seems that the clock resultion results in fReEntry->ttl == ttl
#endif
        ds.addEntry(reEntry);

        REQUIRE(ds.getActiveEntriesCount() == 2);
        auto activeEntries = ds.queryEntries();
        REQUIRE(activeEntries.size() == 2);
        entries       = ds.entries();
        auto fReEntry = std::find(entries.begin(), entries.end(), reEntry);
        REQUIRE(fReEntry != entries.end());
        REQUIRE(fReEntry->ttl > ttl);
        REQUIRE(std::find(activeEntries.begin(), activeEntries.end(), entry_b) != activeEntries.end());
    }
}

TEST_CASE("data storage - persistence", "[DNS]") {
    SECTION("One Entry") {
        const char     *filename = "test1.yas";
        FileDeleter     fd{ filename };
        TestDataStorage ds;
        ds.addEntry(entry_a);
        REQUIRE(ds.save(filename));

        REQUIRE(std::filesystem::exists(filename));
        ds.clear();
        REQUIRE(ds.getActiveEntriesCount() == 0);
        auto bret = ds.load(filename);
        REQUIRE(bret);
        REQUIRE(ds.getActiveEntriesCount() == 1);
    }
    SECTION("Three Entries") {
        const char *filename = "test2.yas";
        FileDeleter fd{ filename };
        if (std::filesystem::exists(filename)) {
            std::filesystem::remove(filename);
        }
        TestDataStorage ds;
        ds.addEntry(entry_a);
        ds.addEntry(entry_b);
        ds.addEntry(entry_c);
        REQUIRE(ds.save(filename));

        REQUIRE(std::filesystem::exists(filename));
        ds.clear();
        REQUIRE(ds.getEntries().empty());
        auto bret = ds.load(filename);
        REQUIRE(bret);
        REQUIRE(ds.getEntries().size() == 3);
        auto entries = ds.getEntries();
        REQUIRE(entries[0] == entry_a);
        REQUIRE(entries[1] == entry_b);
        REQUIRE(entries[2] == entry_c);
    }
}

template<typename T>
T makeRandom(uint32_t size) {
    std::random_device               rd;
    std::mt19937                     generator(rd());
    std::uniform_int_distribution<T> distribution(0, static_cast<T>(size));

    return distribution(generator);
}

template<>
std::string makeRandom(uint32_t size) {
    std::string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    std::string randomString(size, ' ');
    std::generate(randomString.begin(), randomString.end(), [&]() {
        return characters[makeRandom<uint32_t>(static_cast<uint32_t>(characters.length() - 1))];
    });

    return randomString;
}
struct RandomEntry : Entry {
    RandomEntry() {
        protocol     = makeRandom<std::string>(14);
        hostname     = makeRandom<std::string>(14);
        port         = makeRandom<int>(65000);
        service_name = makeRandom<std::string>(14);
        service_type = makeRandom<std::string>(14);
        signal_name  = makeRandom<std::string>(14);
        signal_type  = makeRandom<std::string>(14);
        signal_unit  = makeRandom<std::string>(14);
        signal_rate  = 0.42222f;
    }
};

#ifndef __EMSCRIPTEN__
TEST_CASE("data storage - benchmarking", "[!benchmark][DNS]") {
    std::vector<Entry> entries{ 1000 };
    std::generate(entries.begin(), entries.end(), []() {
        return RandomEntry{};
    });
    TestDataStorage ds;
    BENCHMARK("insert 1000 entries") {
        for (const auto &e : entries) {
            ds.addEntry(e);
        }
    };
    BENCHMARK("find first") {
        QueryEntry qe;
        qe.signal_name = entries[0].signal_name;
        qe.protocol    = entries[0].hostname;
        return ds.queryEntries(qe);
    };
    BENCHMARK("find last") {
        QueryEntry qe;
        qe.signal_name = entries.back().signal_name;
        qe.protocol    = entries.back().hostname;
        return ds.queryEntries(qe);
    };
}
#endif
