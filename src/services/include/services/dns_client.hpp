#ifndef DNS_CLIENT_HPP
#define DNS_CLIENT_HPP


#include <atomic>
#include <chrono>

#include "Debug.hpp"
#include "dns_types.hpp"
#include "MdpMessage.hpp"
#include "RestClient.hpp"
#ifdef EMSCRIPTEN
#include <emscripten/threading.h>
#endif
#include <future>
#include <IoSerialiserYaS.hpp>
#include <thread>
#include <URI.hpp>
#include <utility>
#include <QuerySerialiser.hpp>

namespace opencmw::service::dns {

struct DnsClient;
template<typename T >//TODO hold stuff
struct EmscriptenCallback {
    std::function<T> func;
};

static std::vector<EmscriptenCallback<void(std::vector<Entry>)>> dns_client_callbacks;


struct ClientContextCallback : EmscriptenCallback<void(const mdp::Message &msg)> {
    int index;
};
static std::vector<ClientContextCallback> dns_clientcontext_callbacks;

class DnsClient {
    client::ClientContext &_clientContext;
    URI<STRICT>            _endpoint;

#ifdef EMSCRIPTEN
    static int __set_callback(std::atomic_bool* done, mdp::Message* answer,     std::function<void(const mdp::Message &)> *callback) {
        DEBUG_LOG("yeah, setting callback in main");
        *callback = [] (const mdp::Message &msg) {
            DEBUG_LOG("yeah, callback")
        };
    }
    void _set_callback_on_main(std::atomic_bool* done, mdp::Message* answer, std::function<void(const mdp::Message&)> *callback) {
        emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VIII, __set_callback, done, answer, callback);
    }
#endif // EMSCRIPTEN

public:
    DnsClient(client::ClientContext &clientContext, const URI<STRICT> &endpoint)
        : _clientContext(clientContext)
        , _endpoint(endpoint) {
    }

    static void callbacky(const std::vector<Entry> &response_entries) {
        DEBUG_LOG("callbacky")
    }
    void querySignalsSync() {
        std::promise<std::vector<Entry>> p;
        querySignalsFuture(p);
        auto f = p.get_future();
        while (f.wait_for(std::chrono::microseconds{50}) == std::future_status::timeout) {

        }
    }

    // we need this, because if we create an object from a worker and try to destroy it from the main
    //  thread later, we get an error
    static void prepareMainCallback(int index) {

    }

    void querySignalsAsync(std::function<void(std::vector<Entry>)> callback, const Entry &filter = {}) {
        auto uri = URI<>::factory(_endpoint);
        auto uri2      = std::move(uri).setQuery(query::serialise(filter)).build();
        DEBUG_VARIABLES(uri2.str());
        auto uri3 = new URI<STRICT>{"http://localhost:8055/dns?signal_type=&signal_rate=nan&signal_unit=&service_name=&signal_name=&port=-1&service_type=&hostname=&protocol="};
        static URI<STRICT> finalUri{"http://localhost:8055/dns"};
        auto puri = new URI<>{"http://localhost:8055/dns"};

        std::cout << &callback << finalUri.str() << std::endl;
        //service::dns::dns_client_callbacks.push_back({callback});
        auto index = dns_client_callbacks.size()-1;
        auto ci = dns_clientcontext_callbacks.size();
        dns_clientcontext_callbacks.push_back({
                                               [ci, index](const mdp::Message &msg) {
                    std::cout << msg.error << std::endl;
                    IoBuffer      buf{ msg.data };
                    FlatEntryList resp;
                    //deserialise<YaS, ProtocolCheck::IGNORE>(buf, resp);

                    //callback(resp.toEntries());
                    //dns_client_callbacks[ci].func(resp.toEntries());
                    //msg.

                    DEBUG_LOG("inside clientContext callback");
                    // callback(resp.toEntries());
                }, (int)dns_clientcontext_callbacks.size()});

        //_clientContext.get(*uri3, [](const mdp::Message &msg) {return ;});// dns_clientcontext_callbacks[ci].func);
        _clientContext.get(*uri3, [ci, &callback](const mdp::Message &msg) {
            std::cout << msg.error << std::endl;
            IoBuffer      buf{ msg.data };
            FlatEntryList resp;
            //deserialise<YaS>(buf, resp);
            callbacky({});
            callback(resp.toEntries());
            //dns_client_callbacks[ci].func(resp.toEntries());

            DEBUG_LOG("inside clientContext callback");
            //callback(resp.toEntries());
            });
        DEBUG_LOG("uri gets destroyed? O.O")
    }

    void querySignalsFuture(std::promise<std::vector<Entry>> &promise, const Entry &filter = {}) {
        auto a = new std::function{[&promise](const std::vector<Entry> &response_entries) {
            std::cout << "cool, setting promise" << std::endl;
                promise.set_value(response_entries);
            }};
        querySignalsAsync(*a);
    }
    std::vector<Entry> querySignalsSyncFut() {
        std::promise<std::vector<Entry>> res;
        querySignalsFuture(res);
        auto f = res.get_future();

        while (f.wait_until(std::chrono::system_clock::now()) == std::future_status::timeout) {
            ;
        }
        return f.get();
    }
    std::vector<Entry> querySignals(const Entry &filter = {}) {
        std::atomic_bool   received{ false };
        std::mutex m;
        std::condition_variable cv;
        std::vector<Entry> resp;
     //   auto a = [this,&filter, &received, &resp, &cv](){
            querySignalsAsync([&received, &resp, &cv ](const std::vector<Entry> &response_entries) mutable {
            std::cout << "in async blaaaa" << std::endl;
                resp     = response_entries;

                received = true;
                received.notify_one();
                cv.notify_all();
                std::cout << "outta here" << std::endl;
            },
                    filter);
//        };
  /*      auto k = new std::function<void(std::atomic_bool* b, std::vector<Entry>* v, const Entry* filter)>{
            [](std::atomic_bool* b, std::vector<Entry>* v, const Entry* filter){
                //*v =
            }
        };*/

        //emscripten_sync_run_in_main_runtime_thread_(EM_FUNC_SIG_V, s);
      //  a();
/*        auto loop = [&m, &received, &cv]() {
            std::unique_lock l{m};
            cv.wait_for(l, std::chrono::microseconds {500}, [&received](){return received.load(std::memory_order_relaxed);
            });
        };*/
            DEBUG_FINISH(        received.wait(false, std::memory_order_release) )
        while (true)
        {
            //emscripten_current_thread_process_queued_calls();
           // emscripten_sleep(500);
            std::unique_lock l{m};
            //DEBUG_LOG("locked")
            if (cv.wait_for(l, std::chrono::microseconds {500}, [&received](){
              //  DEBUG_LOG("loading received")
                return received.load(std::memory_order_relaxed);
            })) {
                std::cout << "returning" << std::endl;
                return resp;
            }
        }

        // if you are the mainthread it's a bad idea to wait for this
        DEBUG_FINISH( received.wait(false, std::memory_order_release) )

        return resp;
    }

    void registerSignalsAsync(std::function<void(std::vector<Entry>)> callback, const std::vector<Entry> &entries) {
        auto          uri = URI<>::factory(_endpoint);

        IoBuffer      buf;
        FlatEntryList entrylist{ entries };
        opencmw::serialise<YaS>(buf, entrylist);

/*        _clientContext.set(
                _endpoint, [&callback](auto &msg) {
                    FlatEntryList resp;
                    IoBuffer      buf{ msg.data };
                    deserialise<YaS, ProtocolCheck::ALWAYS>(buf, resp);
                    callback(resp.toEntries());
                },
                std::move(buf)); */
    }

    std::vector<Entry> registerSignals(const std::vector<Entry> &entries) {
        std::atomic_bool   received{ false };
        std::vector<Entry> resp;

        registerSignalsAsync([&received, &resp](const std::vector<Entry> &response_entries) {
            resp     = response_entries;
            received = true;
            received.notify_one();
        },
                entries);

        DEBUG_LOG("wating for results")
        received.wait(false, std::memory_order_relaxed);

        return resp;
    }

    Entry registerSignal(const Entry &entry) {
        return registerSignals({ entry })[0];
    }
};

class DnsRestClient : public opencmw::client::RestClient {
    URI<STRICT> endpoint;

public:
    DnsRestClient(const std::string &uri = "http://localhost:8080/dns")
        : opencmw::client::RestClient(opencmw::client::DefaultContentTypeHeader(MIME::BINARY)), endpoint(uri) {
    }

    std::promise<std::vector<service::dns::Entry>> querySignalsPromise() {

    }

    std::future<dns::FlatEntryList> querySignalsListFuture(std::promise<dns::FlatEntryList>& promise, const Entry &filter = {}) {
        std::promise<dns::FlatEntryList> res;
        auto uri       = URI<>::factory();

        auto queryPara = opencmw::query::serialise(filter);
        uri            = std::move(uri).setQuery(queryPara);

        client::Command cmd;
        cmd.command  = mdp::Command::Get;
        cmd.endpoint = endpoint;
        cmd.callback = [&promise](mdp::Message answer) {
            std::cout << "promise callback" << std::endl;
            FlatEntryList r;
            if (answer.data.size()) {
                //opencmw::deserialise<YaS, ProtocolCheck::ALWAYS>(answer.data, r);

            } else {
                std::cout << "empty result :/" << std::endl;
            }
            promise.set_value(r);
        };
        request(cmd);
        return promise.get_future();
    }
#ifdef EMSCRIPTEN
    static void callbacky(const mdp::Message& reply) {
        std::cout << "callbacky" << std::endl;
    }
    std::function<std::vector<Entry>(const mdp::Message &)> create_waiting_callback_in_main_(/*std::atomic_bool& done, mdp::Message& answer*/) {
        return {[/*&done, &answer*/](const mdp::Message &reply) {
            std::cout << "callback" << std::endl;
            return std::vector<Entry>{};
        }};
    }
    static int __set_callback(std::atomic_bool* done, mdp::Message* answer,     std::function<void(const mdp::Message &)> *callback) {
        DEBUG_LOG("yeah, setting callback in main");
        *callback = [] (const mdp::Message &msg) {
            DEBUG_LOG("yeah, callback")
        };
        return 0;
    }
    void _set_callback_on_main(std::atomic_bool* done, mdp::Message* answer, std::function<void(const mdp::Message&)> *callback) {
        emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VIII, __set_callback, done, answer, callback);
    }
#endif
    std::vector<Entry> querySignals(const Entry &filter = {}) {
        auto uri       = URI<>::factory();

        auto queryPara = opencmw::query::serialise(filter);
        uri            = std::move(uri).setQuery(queryPara);

        client::Command cmd;
        cmd.command  = mdp::Command::Get;
        cmd.endpoint = endpoint;

        std::atomic<bool> done;
        mdp::Message      answer;
        //_set_callback_on_main(&done, &answer, &cmd.callback);// [/*&done, &answer*/](const mdp::Message &reply) {
        cmd.callback = [&done, &answer](const mdp::Message &reply) {
            std::cout << "callback" << std::endl;
            answer = reply;
            done.store(true, std::memory_order_release);
            done.notify_all();
        };
        DEBUG_LOG("requesting");
        request(cmd);

//        std::thread{[&done, &answer](){
//        while (!done.load(std::memory_order_acquire)) {
//            emscripten_main_thread_process_queued_calls();
//        }

        //DEBUG_VARIABLES(emscripten_is_main_browser_thread(), emscripten_is_main_runtime_thread());
        DEBUG_LOG("before wait");
        /*while (!done.load(std::memory_order_acquire)) {
            //emscripten_
        }*/
        done.wait(false, std::memory_order_acquire);
        std::cout << "waited" << std::endl;
//        if (!done.load(std::memory_order_acquire)) {
//            throw std::runtime_error("error acquiring answer");
//        }
//        if (!answer.error.empty()) {
//            throw std::runtime_error{ answer.error };
//        }}}.join();

        FlatEntryList res;
        if (answer.data.empty()) {
            std::cerr << "empty data" << std::endl;
        } else {
            //opencmw::deserialise<YaS, ProtocolCheck::ALWAYS>(answer.data, res);
        }

        return res.toEntries();
    }

    std::vector<Entry> registerSignals(const std::vector<Entry> &entries) {
        IoBuffer outBuffer;
        FlatEntryList entrylist{ entries };
        opencmw::serialise<YaS>(outBuffer, entrylist);

        client::Command cmd;
        cmd.command  = mdp::Command::Set;
        cmd.endpoint = endpoint;
        cmd.data     = outBuffer;

        std::atomic<bool> done;
        mdp::Message      answer;
        cmd.callback = [&done, &answer](const mdp::Message &reply) {
            answer = reply;
            done.store(true, std::memory_order_release);
            done.notify_all();
        };
        request(cmd);

        done.wait(false);
        if (!done.load(std::memory_order_acquire)) {
            throw std::runtime_error("error acquiring answer");
        }
        if (!answer.error.empty()) {
            throw std::runtime_error{ answer.error };
        }

        FlatEntryList res;
        if (!answer.data.empty()) {
            //opencmw::deserialise<YaS, ProtocolCheck::ALWAYS>(answer.data, res);
        }
        return res.toEntries();
    }

    Entry registerSignal(const Entry &entry) {
        return registerSignals({ entry })[0];
    }
};



} // namespace opencmw::service::dns

#endif // DNS_CLIENT_HPP