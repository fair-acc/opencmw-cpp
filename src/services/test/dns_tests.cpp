
#define CATCH_CONFIG_MAIN // This tells the catch header to generate a main
#define CATCH_CONFIG_ENABLE_BENCHMARKING 1

#include "../dns.hpp"
#include <catch2/catch.hpp>
#include <filesystem>
#include <string>

#ifndef __EMSCRIPTEN__
#include "../services.hpp"
#endif

#ifndef __EMSCRIPTEN__
class FileDeleter {
public:
    // make sure to delete datastorage file when finishing
    FileDeleter(std::string filename = "dns_data_storage.yas")
        : filename(filename) {
        deleteFile();
    }
    ~FileDeleter() {
        deleteFile();
    }
    void deleteFile() {
        if (std::filesystem::exists(filename)) {
            std::filesystem::remove(filename);
        }
    }
    std::string filename;
};

FileDeleter dsFd; // delete DataStorage file when finishing

#else
class FileDeleter {
public:
    FileDeleter(std::string filename) {}
};
#endif // __EMSCRIPTEN__

using namespace opencmw::service::dns;

Entry a{ "http", "localhost", 8080, "group a", "unknown", "A", "ms", 0.123456, "" };
Entry b{ "http", "localhost", 8080, "group a", "unknown", "B", "ms", 2.3223, "" };
Entry c{ "http", "localhost", 8080, "test", "unknown", "C", "ms", 3.333, "" };

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
    TestDataStorage(std::string filename = "test_.yas") {
        clear();
    }
    ~TestDataStorage() {}
};

TEST_CASE("type tests", "[DNS") {
    SECTION("QueryResponse") {
        std::vector<Entry> entries{ a, b, c };
        QueryResponse      response{ { a, b, c } };
        auto               newEntries = response.toEntries();
        REQUIRE(newEntries[0] == a);
        REQUIRE(newEntries[1] == b);
        REQUIRE(newEntries[2] == c);
    }
    SECTION("QueryEntry") {
        QueryEntry qa;
        qa.signal_name = "A";
        QueryEntry qb;
        qb.signal_name = "B";
        QueryEntry qh;
        qh.protocol = "http";
        REQUIRE(QueryEntry{} == a); // should match all
        REQUIRE(qa == a);
        REQUIRE(qb != a);
        REQUIRE(qb == b);
        REQUIRE(qh == a);
        REQUIRE(qh == b);
    }
}

#ifndef __EMSCRIPTEN__
TEST_CASE("run services", "[DNS]") {
    FileDeleter                        fd;
    opencmw::service::RunDefaultBroker broker;
    broker.runWorker<dnsWorker, DnsWorker>();
    broker.startBroker();
}
#endif

TEST_CASE("data storage - Adding Entries", "[DNS]") {
    TestDataStorage ds;

    REQUIRE(ds.getActiveEntriesCount() == 0);
    ds.addEntry(a);
    REQUIRE(ds.getActiveEntriesCount() == 1);
    ds.addEntry(b);
    REQUIRE(ds.getActiveEntriesCount() == 2);
    auto entries = ds.getEntries();
    REQUIRE(entries[0] == a);
    REQUIRE(entries[1] == b);
}

TEST_CASE("data storage - Querying Entries") {
    TestDataStorage ds;
    ds.addEntry(a);
    ds.addEntry(b);
    ds.addEntry(c);
    REQUIRE(ds.getActiveEntriesCount() == 3);
    QueryEntry qc;
    qc.signal_name = "C";
    QueryEntry qGroupA;
    qGroupA.service_name = "group a";

    auto entries         = ds.queryEntries(qc);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0] == c);

    entries = ds.queryEntries(qGroupA);
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0] == a);
    REQUIRE(entries[1] == b);
}

TEST_CASE("data storage - Renewing Entries") {
    TestDataStorage ds;
    ds.addEntry(a);
    ds.addEntry(b);
    ds.addEntry(a);
    auto now = StorageEntry::clock::now();

    REQUIRE(ds.getActiveEntriesCount() == 2);

    auto entries = ds.queryEntries(a);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0] == a);
}

#ifndef __EMSCRIPTEN__
TEST_CASE("client", "[DNS]") {
    FileDeleter fd;
    opencmw::service::RunDefaultBroker broker;
    broker.runWorker<dnsWorker, DnsWorker>();
    broker.startBroker();

    DnsClient client;

    //client.query
}
TEST_CASE("rest client", "[DNS]") {
    FileDeleter                        fd;
    opencmw::service::RunDefaultBroker broker;
    broker.runWorker<dnsWorker, DnsWorker>();
    broker.startBroker();

    DnsRestClient client;

    auto      services = client.queryServices();
    REQUIRE(services.size() == 0);

    auto ret = client.registerService(a);
    REQUIRE(ret.signal_rate == a.signal_rate);
    ret = client.registerService(b);
    REQUIRE(ret == b);
    ret = client.registerService(c);
    REQUIRE(ret == c);

    services = client.queryServices();
    // TODO REQUIRE(services.size() == storage.getActiveEntriesCount());
}

TEST_CASE("query", "[DNS]") {
    FileDeleter                        fd;
    opencmw::service::RunDefaultBroker broker;
    broker.runWorker<dnsWorker, DnsWorker>();
    broker.startBroker();

    SECTION("query") {
        auto services = queryServices();
        REQUIRE(services.size() == 0);
        registerService(a);
        services = queryServices();
        REQUIRE(services.size() == 1);
        REQUIRE(services.at(0) == a);
        registerService(b);
        registerService(c);
        services = queryServices();

        REQUIRE(3 == services.size());
        REQUIRE(std::ranges::equal(services, std::vector<Entry>{ a, b, c }));
    }

    SECTION("query with filters") {
        auto services = queryServices({ .signal_name = "C" });
        REQUIRE(services.size() == 0);
        registerService(c);
        services = queryServices({ .signal_name = "C" });
        REQUIRE(services.size() == 1);
        REQUIRE(services[0] == c);
    }
}
#endif // __EMSCRIPTEN__

TEST_CASE("registering", "[DNS]") {
    SECTION("registering service") {
        TestDataStorage ds;

        ds.addEntry(a);
        REQUIRE(ds.queryEntries().size() == 1);
        REQUIRE(ds.queryEntries()[0] == a);
    }
    SECTION("unregistering service when not reregistered") {
        TestDataStorage ds;
        ds.addEntry(a);
        ds.addEntry(b);
        ds.addEntry(c);

        auto &entries      = ds.entries();

        entries[0].ttl     = std::chrono::system_clock::now();
        entries[2].ttl     = std::chrono::system_clock::now();

        auto activeEntries = ds.queryEntries();
        REQUIRE(activeEntries.size() == 1);
        REQUIRE(activeEntries[0] == b);
    }

    SECTION("reregistering service") {
        TestDataStorage ds;
        ds.addEntry(a);
        ds.addEntry(b);
        ds.addEntry(c);
        auto &entries  = ds.entries();

        entries[0].ttl = std::chrono::system_clock::now();
        entries[2].ttl = std::chrono::system_clock::now();
        auto ttl       = entries[2].ttl;
        auto reEntry   = entries[2];
        ds.addEntry(reEntry);

        REQUIRE(ds.getActiveEntriesCount() == 2);
        auto activeEntries = ds.queryEntries();
        REQUIRE(activeEntries.size() == 2);
        REQUIRE(std::find(activeEntries.begin(), activeEntries.end(), reEntry) != activeEntries.end());
        REQUIRE(std::find(activeEntries.begin(), activeEntries.end(), b) != activeEntries.end());
    }
}

TEST_CASE("data storage - persistence", "[DNS]") {
    SECTION("One Entry") {
        const char     *filename = "test1.yas";
        FileDeleter     fd{ filename };
        TestDataStorage ds;
        ds.addEntry(a);
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
        ds.addEntry(a);
        ds.addEntry(b);
        ds.addEntry(c);
        REQUIRE(ds.save(filename));

        REQUIRE(std::filesystem::exists(filename));
        ds.clear();
        REQUIRE(ds.getEntries().size() == 0);
        auto bret = ds.load(filename);
        REQUIRE(bret);
        REQUIRE(ds.getEntries().size() == 3);
        auto entries = ds.getEntries();
        REQUIRE(entries[0] == a);
        REQUIRE(entries[1] == b);
        REQUIRE(entries[2] == c);
    }
}

template<typename T>
T makeRandom(uint32_t size);

template<>
uint32_t makeRandom(uint32_t size) {
    std::random_device                      rd;
    std::mt19937                            generator(rd());
    std::uniform_int_distribution<uint32_t> distribution(0, size);

    return distribution(generator);
}

template<>
std::string makeRandom(uint32_t size) {
    std::string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    std::string randomString(size, ' ');
    std::generate(randomString.begin(), randomString.end(), [&]() {
        return characters[makeRandom<uint32_t>(characters.length() - 1)];
    });

    return randomString;
}
struct RandomEntry : Entry {
    RandomEntry() {
        protocol     = makeRandom<std::string>(14);
        hostname     = makeRandom<std::string>(14);
        port         = makeRandom<uint32_t>(65000);
        service_name = makeRandom<std::string>(14);
        service_type = makeRandom<std::string>(14);
        signal_name  = makeRandom<std::string>(14);
        signal_type  = makeRandom<std::string>(14);
        signal_unit  = makeRandom<std::string>(14);
        signal_rate  = 0.42222;
    }
};

#ifndef __EMSCRIPTEN__
TEST_CASE("data storage - benchmarking", "[DNS]") {
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