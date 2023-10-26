#ifdef EMSCRIPTEN
#include <emscripten/emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/trace.h>
#endif

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <utility>

#include "Debug.hpp"
#include <services/dns_client.hpp>

#include <IoSerialiserYaS.hpp>
#include <services/dns_types.hpp>
/*
 to run tests:
   in the browser
 run the server: concepts/client ./dns_example server http://localhost:8055/dns mdp://inproc
   in node
 npm install xhr2
*/

// for proper operation many implementations need a main loop which returns control to the main/js thread
//  do this WITH_MAINLOOP, setting a main loop and exiting main into it
// #define TEST_WITH_MAINLOOP
// #define CREATE_TESTCASES_ON_STACK // not fully imnpl/tested

// #define TESTS_RUN_IN_THREADS
// #define RUN_TESTS_BEFORE_MAIN
#define RUN_WITH_TIMEOUT \
    std::chrono::seconds { 3 }

/* one solution with ClientContext and a main loop set with em_set_mainloop
   is to proxy the requests to the main thread,
#define PROXY_FETCH_TO_MAIN

// add this to RestClientEmscripten, beware, there might be problems with the callback
#ifdef PROXY_FETCH_TO_MAIN
if (emscripten_is_main_runtime_thread()) {
    emscripten_fetch(&attr, d);
} else {
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VII, fetch, attr, d);
    std::cout << "proxied to main thread" << std::endl;
}
#else // PROXY_FETCH_TO_MAIN
emscripten_fetch(&attr, d);
#endif

 */

// #define TEST_SIMPLE_REQUEST
#define TEST_THREADED_REQUEST
//
// ; fetch fails
#define TEST_SIMPLE_REQUEST_WAITABLE
// there is thread proxying in emscripten, but this doesn't help, because fetches are worked upon in the runtime thread
// works in mainloop;
#define TEST_FETCH_PROXYING_RETURNER
#define TEST_FETCH_PROXYING_LOOPER
// uses DnsRestClient but build command by hand
// works in mainloop; fetch fails immediately in node and build down throws, fetch never fetched in chromium
// I had to rewrite this test, and something seems to broken
// #define TEST_REST_CLIENT_MANUAL
#define TEST_CONTEXTCLIENT_FUTURE
#define TEST_REST_CLIENT_SYNC

#if defined(TEST_FETCH_PROXYING_RETURNER) or defined(TEST_FETCH_PROXYING_LOOPER) or defined(TEST_THREADED_REQUEST)
#define NEED_SIMPLE_REQUEST
#endif

using namespace opencmw;
struct TestCase;

static std::vector<TestCase *> tests;

struct TestCase {
    enum TestState {
        INITIALISED,
        RUNNING,
        FINISHED_UNKNOWN,
        FINISHED_FAILURE,
        FINISHED_SUCCESS,
    } state
            = INITIALISED;

    TestCase(std::string testname = "test")
        : name(testname) {
        tests.push_back(this);
    }

    virtual void _run() = 0;
    void         run() {
        DEBUG_LOG("Running " + name);
        _run();
        DEBUG_LOG(name + " ran through")
        state = RUNNING;
    }
    virtual bool ready() {
        return state > RUNNING;
    }
    virtual bool success() { return state == FINISHED_SUCCESS; }
    virtual ~TestCase() = default;
    std::string name;
};

template<typename T>
struct FutureTestcase : TestCase {
    using TestCase::TestCase;
    std::promise<T> promise;
    std::future<T>  future;
    bool            ready() override {
        return future.wait_for(std::chrono::seconds{ 0 }) != std::future_status::timeout;
    }
};

#ifdef CREATE_TESTCASES_ON_STACK
#define ADD_TESTCASE(testcasetype) testcasetype _##testcasetype{ #testcasetype };
#else
#define ADD_TESTCASE(testcasetype) testcasetype *_##testcasetype = new testcasetype{ #testcasetype };
#endif

#ifdef RUN_WITH_TIMEOUT
/**
 * The `Timeout` class records the time of creation and allows you to check
 * whether a specified timeout duration has passed since its creation.
 */
class Timeout {
public:
    explicit Timeout(const std::chrono::steady_clock::duration &duration)
        : start_time_(std::chrono::steady_clock::now()), duration_(duration) {}

    /* @brief Check if the timeout duration has passed.
     * @return `true` if the timeout duration has passed, `false` otherwise.
     */
    bool operator()() const {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = current_time - start_time_;
        return elapsed_time >= duration_;
    }

private:
    std::chrono::steady_clock::time_point start_time_; ///< The time of creation.
    std::chrono::steady_clock::duration   duration_;   ///< The timeout duration.
};
#endif // RUN_WITH_TIMEOUT

template<typename Range, typename Predicate>
auto find_all(Range &&range, Predicate &&predicate) {
    return std::views::filter(std::forward<Range>(range), std::forward<Predicate>(predicate));
}
// returns found elements as a " " concatenated string
auto find_all_string(const std::vector<TestCase *> &tests, auto predicate) {
    std::string res;
    for (const auto &test : find_all(tests, predicate)) {
        res += test->name + " ";
    }
    return res;
}

void run_tests() {
    std::for_each(tests.begin(), tests.end(), [](TestCase *t) {
        DEBUG_LOG(t->name);
#ifdef TESTS_RUN_IN_THREADS
        std::thread{ [t]() { t->run(); } }.detach();
#else
                t->run();
                std::cout << "ready " << t->ready() << std::endl;
#endif
    });
}

std::string still_running_tests() {
    std::string res = find_all_string(tests, [](TestCase *t) {
        return !t->ready();
    });
    DEBUG_VARIABLES(res)
#ifdef RUN_WITH_TIMEOUT
    static Timeout testsTimeout{ RUN_WITH_TIMEOUT };
    if (res != "" && testsTimeout()) {
        DEBUG_LOG("Timeout hit, the following Tests are still running and will be cancelled - " + res)
        return "";
    }
#endif // RUN_WITH_TIMEOUT
    return res;
}

#if defined(TEST_SIMPLE_REQUEST) or defined(NEED_SIMPLE_REQUEST)

struct TestEmscriptenRequest : TestCase {
    using TestCase::TestCase;

    std::atomic_bool dataReady{ false };

    void             _run() override {
        DEBUG_LOG("requesting " + name);
        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        strcpy(attr.requestMethod, "GET");
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.onsuccess  = TestEmscriptenRequest::downloadSucceeded;
        attr.onerror    = TestEmscriptenRequest::downloadFailed;
        attr.userData   = this;
        emscripten_fetch(&attr, "http://localhost:8055/dns");
    }

    static void downloadSucceeded(emscripten_fetch_t *fetch) {
        TestEmscriptenRequest *self = static_cast<TestEmscriptenRequest *>(fetch->userData);
        self->state                 = FINISHED_SUCCESS;
        self->dataReady.store(true);
        self->dataReady.notify_all();
        DEBUG_LOG("FINISHED - " + self->name)

        printf("Finished downloading %llu bytes from URL %s.\n", fetch->numBytes, fetch->url);
        // The data is now available at fetch->data[0] through fetch->data[fetch->numBytes-1];
        emscripten_fetch_close(fetch); // Free data associated with the fetch.
    }

    static void downloadFailed(emscripten_fetch_t *fetch) {
        TestEmscriptenRequest *self = static_cast<TestEmscriptenRequest *>(fetch->userData);
        self->state                 = FINISHED_FAILURE;
        printf("Finished downloading %llu bytes from URL %s.\n", fetch->numBytes, fetch->url);
        // The data is now available at fetch->data[0] through fetch->data[fetch->numBytes-1];
        emscripten_fetch_close(fetch); // Free data associated with the fetch.
    }
};
#endif // TEST/NEED _SIMPLE_REQUEST

#ifdef TEST_SIMPLE_REQUEST
ADD_TESTCASE(TestEmscriptenRequest)
#endif

#ifdef TEST_THREADED_REQUEST
struct TestThreadedRequest : TestEmscriptenRequest {
    using TestEmscriptenRequest::TestEmscriptenRequest;
    std::thread t;
    void        _run() override {
        t = std::thread{
            [this]() { TestEmscriptenRequest::_run(); }
        };
    }
    ~TestThreadedRequest() {
        t.join();
    }
};
ADD_TESTCASE(TestThreadedRequest)
#endif // TEST_THREADED_REQUEST

#ifdef TEST_FETCH_PROXYING_RETURNER
// https://emscripten.org/docs/api_reference/proxying.h.html
//  from example https://github.com/emscripten-core/emscripten/blob/main/test/pthread/test_pthread_proxying_cpp.cpp#L37
#include <emscripten/eventloop.h>
#include <emscripten/proxying.h>
#include <sched.h>

struct TestFetchProxyingReturner : TestCase {
    using TestCase::TestCase;
    std::thread               returner{ [this]() { this->returner_run(); } };
    std::atomic_bool          should_quit{ false };
    emscripten::ProxyingQueue queue;
    TestEmscriptenRequest     tr{ "ProxyingReturner" }, tr2{ "ProxyingReturner2" };

    void                      returner_run() {
        DEBUG_LOG("returner_run")
        // Return back to the event loop while keeping the runtime alive.
        // Note that we can't use `emscripten_exit_with_live_runtime` here without
        // introducing a memory leak due to way to C++11 threads interact with
        // unwinding. See https://github.com/emscripten-core/emscripten/issues/17091.
        if (!should_quit || (tr.ready() && tr2.ready()))
            emscripten_runtime_keepalive_push();
        else
            DEBUG_LOG("~returner_run")
    }

    bool ready() override {
        state = std::min(tr.state, tr2.state);
        return state != RUNNING;
    }

    void _run() {
        queue.proxyAsync(returner.native_handle(), [&]() { tr.run(); tr2.run(); });
    }
};
ADD_TESTCASE(TestFetchProxyingReturner)

#endif // TEST_FETCH_PROXYYING_RETURNER

#ifdef TEST_FETCH_PROXYYING_LOOPER
struct TestFetchProxyingLooper : TestCase {
    emscripten::ProxyingQueue queue;
    using TestCase::TestCase;
    std::thread      looper{ [this]() { this->looper_run(); } };
    std::atomic_bool should_quit{ false };

    // the requests used by l_ooper r_eturner and the threaded variant
    TestEmscriptenRequest tl{ "ProxyingLooper" }, tr{ "ProxyingReturner" }, t{ "ProxyingThreaded" }, tl2{ "ProxyingLooper2" }, tr2{ "ProxyingReturner2" }, t2{ "ProxyingThreaded2" };

    void                  looper_run() {
        DEBUG_LOG("looper_run")
        while (!should_quit.load() && (!tl.ready() && !tl2.ready())) {
            DEBUG_LOG_EVERY_SECOND("LOOOOOOPING")
            DEBUG_LOG_EVERY_SECOND(should_quit)
            queue.execute();
            sched_yield();
        }
        DEBUG_LOG("!looper_run")
    }

    bool ready() {
        auto a = tl.ready() && tl2.ready();
        if (tl.ready() && tr.ready()) {
            should_quit = true;
        }
        return a;
    }

    void _run() {
        queue.proxyAsync(looper.native_handle(), [&]() { tr.run(); tr2.run(); });
        state = RUNNING;
    }

    ~TestFetchProxyingLooper() {
        should_quit = true;
        DEBUG_LOG("~TestFetchProxying")
        std::this_thread::yield();

        looper.detach(); // we will leak here, but at least we don't crash our test application
    }
};
ADD_TESTCASE(TestFetchProxyingLooper)

#endif // TEST_FETCH_PROXYYING_LOOPER

#ifdef TEST_SIMPLE_REQUEST_WAITABLE
struct TestEmscriptenWaitableRequest : public TestCase {
    using TestCase::TestCase;
    EMSCRIPTEN_RESULT   ret = EMSCRIPTEN_RESULT_TIMED_OUT;
    emscripten_fetch_t *fetch;

    void                _run() {
        std::thread{ [this]() {
            emscripten_fetch_attr_t attr;
            emscripten_fetch_attr_init(&attr);
            strcpy(attr.requestMethod, "GET");
            attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_WAITABLE;
            fetch           = emscripten_fetch(&attr, "http://localhost:8055/dns");

            if (ready())
                state = (fetch->numBytes > 0 ? FINISHED_SUCCESS : FINISHED_FAILURE);
        } }.detach();
    }
    /*bool ready() {
       return emscripten_fetch_wait(fetch, 0) != EMSCRIPTEN_RESULT_TIMED_OUT;
    }*/
};
ADD_TESTCASE(TestEmscriptenWaitableRequest)
#endif // TEST_SIMPLE_REQUEST_WAITABLE

#ifdef TEST_REST_CLIENT_MANUAL
// i had to rewrite this test, and something seems to broken
struct TestRestClientManual : FutureTestcase<bool> {
    using FutureTestcase<bool>::FutureTestcase;

    void _run() {
        client::RestClient cl(client::DefaultContentTypeHeader{ MIME::BINARY });

        future = promise.get_future();
        client::Command getRequest;
        getRequest.command  = mdp::Command::Get;
        getRequest.endpoint = URI<>{ "http://localhost:8055/dns" };
        getRequest.callback = [this](const mdp::Message &reply) {
            DEBUG_LOG("")
            if (reply.data.size() > 300 && reply.error == "") {
                std::cout << "getRequest success" << std::endl;
                promise.set_value(true);
            } else {
                promise.set_value(false);
            }
        };
        cl.request(getRequest);
    }
};
ADD_TESTCASE(TestRestClientManual)
#endif // TEST_REST_CLIENT_MANUAL

#ifdef TEST_REST_CLIENT_SYNC
struct TestRestClientSync : FutureTestcase<service::dns::FlatEntryList> {
    using FutureTestcase<service::dns::FlatEntryList>::FutureTestcase;
    void _run() override {
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
        clients.emplace_back(std::make_unique<client::RestClient>(opencmw::client::DefaultContentTypeHeader(MIME::BINARY)));
        client::ClientContext   cl{ std::move(clients) };

        service::dns::DnsClient r{ cl, "http://localhost:8055/dns" };
        promise.set_value(r.querySignals());
        state = FINISHED_SUCCESS;
    }
};
ADD_TESTCASE(TestRestClientSync)
#endif

#ifdef TEST_CONTEXTCLIENT_FUTURE
struct TestContextClient : FutureTestcase<std::vector<service::dns::Entry>> {
    using FutureTestcase<std::vector<service::dns::Entry>>::FutureTestcase;
    std::unique_ptr<client::ClientContext>   clientContext;
    std::unique_ptr<service::dns::DnsClient> dnsClient;

    TestContextClient(std::string name)
        : FutureTestcase<std::vector<service::dns::Entry>>(name) {
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
        clients.emplace_back(std::make_unique<client::RestClient>(opencmw::client::DefaultContentTypeHeader(MIME::BINARY)));
        clientContext = std::make_unique<client::ClientContext>(std::move(clients));
        dnsClient     = std::make_unique<service::dns::DnsClient>(*clientContext.get(), URI<>{ "http://localhost:8055/dns" });

        future        = promise.get_future();
    }

    void _run() {
        std::vector<service::dns::Entry> signals;

        DEBUG_FINISH(signals = dnsClient.get()->querySignals();)

        clientContext->stop();
        promise.set_value(signals);
        state = FINISHED_SUCCESS;
    }
};
ADD_TESTCASE(TestContextClient)
#endif

void wait_for_results() {
    static bool ran_once{ false }; // in case we run tests without result
    if (!ran_once) {
        ran_once = true;
        return;
    }
    DEBUG_LOG_EVERY_SECOND("waiting for results");

    if (auto s = still_running_tests() != "") {
        DEBUG_LOG_EVERY_SECOND("waiting for " + still_running_tests())
    } else {
        DEBUG_LOG("~wait_for_results")

        // Output test names in different categories
        std::cout << "Successful tests: " << find_all_string(tests, [](auto *a) { return a->state == TestCase::FINISHED_SUCCESS; }) << std::endl;
        std::cout << "Failed tests: " << find_all_string(tests, [](auto *a) { return a->state == TestCase::FINISHED_FAILURE || a->state == TestCase::FINISHED_UNKNOWN; }) << std::endl;
        std::cout << "Still Running tests: " << find_all_string(tests, [](auto *a) { return a->state == TestCase::RUNNING; }) << std::endl;

#ifndef EXIT_RUNTIME
        emscripten_run_script("if (serverOnExit) serverOnExit();");
#endif

#if defined(TEST_WITH_MAINLOOP) & defined(TEST_MAINLOOP_CANCEL)
        emscripten_cancel_main_looop();
//        emscripten_force_exit(0);
//        exit(0);
//        emscripten_cancel_main_loop();
#else

        // emscripten_run_script("try { PThread.terminateAllThreads(); } catch(error) {}");
        emscripten_force_exit(0);
#endif
    }
}

int main(int argc, char *argv[]) {
    emscripten_trace_configure_for_google_wtf();

    if (argc > 1) { // only execute selected Tests
        std::cout << "selecting tests " << std::accumulate(argv + 1, argv + argc, std::string{}, [](const std::string &acc, const char *arg) {
            return acc.empty() ? arg : acc + ' ' + arg;
        });
        auto r = std::remove_if(tests.begin(), tests.end(), [argv, argc](TestCase *t) {
            return std::all_of(argv + 1, argv + argc, [t](auto e) { return e != t->name; });
        });
        tests.erase(r, tests.end());
    }

#ifdef EXIT_RUNTIME
    emscripten_run_script("addOnExit(serverOnExit);");
#endif

#ifdef TEST_WITH_MAINLOOP
    emscripten_set_main_loop(wait_for_results, 30, 0);
    DEBUG_LOG("main loop set")
    // we are running the tests here. they may not wait for fetch results, because
    // we have to return from main to put the new main loop in effect
    run_tests();
#else
    run_tests();

    while (std::any_of(tests.begin(), tests.end(),
            [](TestCase *t) { return !t->ready(); })) {
        wait_for_results();
        // throw every conceivable wait, queue process,... in here for good measure
        //   spoiler: doesn't change a thing
        emscripten_pause_main_loop();
        emscripten_main_thread_process_queued_calls();
        emscripten_current_thread_process_queued_calls();
        emscripten_thread_sleep(500);
        //        // emscripten_sleep would help, but this requires ASYNCIFY, which is incompatible with exceptions
        //        // emscripten_sleep(500);
        std::this_thread::yield();
        sched_yield();
    }
#endif

    // We have to exit the main thread for the javascript runtime to run
    DEBUG_LOG("~main")
}
