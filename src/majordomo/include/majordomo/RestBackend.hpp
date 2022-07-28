#ifndef OPENCMW_MAJORDOMO_RESTBACKEND_P_H
#define OPENCMW_MAJORDOMO_RESTBACKEND_P_H

// STD
#include <filesystem>
#include <optional>
#include <regex>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#define CPPHTTPLIB_THREAD_POOL_COUNT 8
#include <httplib.h>
#pragma GCC diagnostic pop

// Core
#include <MIME.hpp>
#include <URI.hpp>

#include <IoSerialiserJson.hpp>
#include <MustacheSerialiser.hpp>
#include <ThreadAffinity.hpp>

// Majordomo
#include <majordomo/Broker.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/ZmqPtr.hpp>

#include <refl.hpp>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(assets);

struct FormData {
    std::unordered_map<std::string, std::string> fields;
};
ENABLE_REFLECTION_FOR(FormData, fields)

struct Service {
    std::string name;
    std::string description;
};
ENABLE_REFLECTION_FOR(Service, name, description)

struct ServicesList {
    std::vector<Service> services;
};
ENABLE_REFLECTION_FOR(ServicesList, services)

namespace opencmw::majordomo {

using namespace std::chrono_literals;

constexpr auto HTTP_OK           = 200;
constexpr auto HTTP_ERROR        = 500;
constexpr auto DEFAULT_REST_PORT = 8080;
constexpr auto REST_POLLING_TIME = 10s;

// Provides a safe alternative to getenv
const char *getEnvFilenameOr(const char *field, const char *defaultValue) {
    const char *result = ::getenv(field);
    if (result == nullptr) {
        result = defaultValue;
    }

    if (!std::filesystem::exists(result)) {
        throw opencmw::startup_error(fmt::format("File {} not found. Current path is {}", result, std::filesystem::current_path().string()));
    }
    return result;
}

struct HTTPS {
    constexpr static std::string_view DEFAULT_REST_SCHEME = "https";
    httplib::SSLServer                _svr;

    HTTPS()
        : _svr(getEnvFilenameOr("OPENCMW_REST_CERT_FILE", "demo_public.crt"), getEnvFilenameOr("OPENCMW_REST_PRIVATE_KEY_FILE", "demo_private.key")) {}
};

struct PLAIN_HTTP {
    constexpr static std::string_view DEFAULT_REST_SCHEME = "https";
    httplib::Server                   _svr;
};

template<typename Mode, typename VirtualFS, role... Roles>
class RestBackend : public Mode {
protected:
    Broker<Roles...> &_broker;
    const VirtualFS  &_vfs;
    std::jthread      _thread;
    URI<>             _restAddress;

    using Mode::_svr;
    using Mode::DEFAULT_REST_SCHEME;

protected:
    struct RestWorker {
        Socket         dealer;
        Socket         subscriber;
        zmq_pollitem_t pollItem{};

        RestWorker(const Broker<Roles...> &broker)
            : dealer(broker.context, ZMQ_DEALER)
            , subscriber(broker.context, ZMQ_SUB) {
            pollItem.events = ZMQ_POLLIN;

            zmq_invoke(zmq_connect, dealer, INTERNAL_ADDRESS_BROKER.str).onFailure<opencmw::startup_error>("Can not connect REST worker to Majordomo broker");
            zmq_invoke(zmq_connect, subscriber, INTERNAL_ADDRESS_PUBLISHER.str).onFailure<opencmw::startup_error>("Can not connect REST worker to Majordomo broker");
        }

        RestWorker(RestWorker &&other) = default;
    };

    // Function that returns this thread's worker instance
    RestWorker &workerForThisThread() {
        thread_local static RestWorker worker(_broker);
        return worker;
    }

protected:
    void respondWithError(auto &response, std::string_view message) {
        response.status = HTTP_ERROR;
        response.set_content(message.data(), MIME::TEXT.typeName().data());
    };

    std::string_view acceptedMimeForRequest(const auto &request) {
        static constexpr std::array<std::string_view, 3> acceptableMimeTypes = {
            MIME::JSON.typeName(), MIME::HTML.typeName(), "application/x-opencmw-test-format"
        };
        auto accepted = [](auto format) {
            const auto it = std::find(acceptableMimeTypes.cbegin(), acceptableMimeTypes.cend(), format);
            return std::make_pair(
                    it != acceptableMimeTypes.cend(),
                    it);
        };

        if (request.has_param("contentType")) {
            std::string format = request.get_param_value("contentType");
            if (const auto [found, where] = accepted(format); found) {
                return *where;
            }
        }

        auto        isDelimiter  = [](char c) { return c == ' ' || c == ','; };
        const auto &acceptHeader = request.get_header_value("Accept");
        auto        from         = acceptHeader.cbegin();
        const auto  end          = acceptHeader.cend();

        while (from != end) {
            from    = std::find_if_not(from, end, isDelimiter);
            auto to = std::find_if(from, end, isDelimiter);
            if (from != end) {
                std::string_view format(from, to);
                if (const auto [found, where] = accepted(format); found) {
                    return *where;
                }
            }

            from = to;
        }

        return acceptableMimeTypes[0];
    }

public:
    explicit RestBackend(Broker<Roles...> &broker, const VirtualFS &vfs, URI<> restAddress = URI<>::factory().scheme(DEFAULT_REST_SCHEME).hostName("0.0.0.0").port(DEFAULT_REST_PORT).build())
        : _broker(broker), _vfs(vfs), _restAddress(restAddress) {
        _broker.registerDnsAddress(restAddress);
        _thread = std::jthread(&RestBackend::run, this);
    }

    virtual ~RestBackend() {
        _svr.stop();
    }

    virtual void registerHandlers() {
        auto defaultHandler = [this](const httplib::Request &request, httplib::Response &response, const httplib::ContentReader *content_reader_ = nullptr) {
            auto                            isDelimiter = [](char c) { return c == '/'; };

            const auto                     &path        = request.path;
            const auto                      end         = path.cend();
            std::array<std::string_view, 2> pathComponents;
            std::size_t                     pathComponentCount = 0;

            // Skip all the slashes at the start of the path, and
            // find the first component
            auto from                            = std::find_if_not(path.cbegin(), end, isDelimiter);
            auto to                              = std::find_if(from, end, isDelimiter);

            pathComponents[pathComponentCount++] = std::string_view(from, to);
            if (from != to && to != path.cend()) {
                pathComponents[pathComponentCount++] = std::string_view(to, path.cend());
            }

            const std::string_view &service = pathComponents[0];
            std::string             topic   = std::string(pathComponents[1]);

            if (service.empty()) {
                // Mmi is not a MajordomoWorker, so it doesn't know JSON (TODO)
                const auto acceptedFormat = acceptedMimeForRequest(request);

                if (acceptedFormat == MIME::JSON.typeName()) {
                    std::vector<std::string> serviceNames;
                    _broker.forEachService([&](std::string_view name, std::string_view) {
                        serviceNames.emplace_back(std::string(name));
                    });
                    response.status = HTTP_OK;

                    opencmw::IoBuffer buffer;
                    // opencmw::serialise<opencmw::Json>(buffer, serviceNames);
                    IoSerialiser<opencmw::Json, decltype(serviceNames)>::serialise(buffer, FieldDescriptionShort{}, serviceNames);
                    response.set_content(buffer.asString().data(), MIME::JSON.typeName().data());

                } else if (acceptedFormat == MIME::HTML.typeName()) {
                    response.set_chunked_content_provider(
                            MIME::HTML.typeName().data(),
                            [this](std::size_t /*offset*/, httplib::DataSink &sink) {
                                ServicesList servicesList;
                                _broker.forEachService([&](std::string_view name, std::string_view description) {
                                    servicesList.services.emplace_back(std::string(name), std::string(description));
                                });

                                // sort services, move mmi. services to the end
                                auto serviceLessThan = [](const auto &lhs, const auto &rhs) {
                                    const auto lhsIsMmi = lhs.name.starts_with("mmi.");
                                    const auto rhsIsMmi = rhs.name.starts_with("mmi.");
                                    if (lhsIsMmi != rhsIsMmi) {
                                        return rhsIsMmi;
                                    }
                                    return lhs.name < rhs.name;
                                };
                                std::sort(servicesList.services.begin(), servicesList.services.end(), serviceLessThan);

                                using namespace std::string_literals;
                                mustache::serialise("ServicesList", sink.os,
                                        std::pair<std::string, const ServicesList &>{ "servicesList"s, servicesList });
                                sink.done();
                                return true;
                            });
                }
            } else {
                auto      &worker = workerForThisThread();

                const auto method = [&]() -> std::string {
                    if (request.has_header("X-OPENCMW-METHOD")) {
                        return request.get_header_value("X-OPENCMW-METHOD");
                    } else {
                        return request.method;
                    }
                }();

                // clang-format off
                auto  message = MdpMessage::createClientMessage(
                         method == "SUB"  ? Command::Subscribe :
                         method == "POLL" ? Command::Subscribe :
                         method == "PUT"  ? Command::Set :
                         method == "POST" ? Command::Set :
                              /* default */ Command::Get);
                // clang-format on
                message.setServiceName(service, MessageFrame::dynamic_bytes_tag{});

                auto        uri = URI<>::factory();
                std::string bodyOverride;
                for (const auto &[key, value] : request.params) {
                    if (key == "_bodyOverride") {
                        bodyOverride = value;
                    } else {
                        uri = std::move(uri).addQueryParameter(key, value);
                    }
                }

                const auto acceptedFormat = acceptedMimeForRequest(request);
                uri                       = std::move(uri).addQueryParameter("contentType", std::string(acceptedFormat));

                topic.append(uri.toString());

                message.setTopic(topic, MessageFrame::dynamic_bytes_tag{});

                if (request.is_multipart_form_data()) {
                    if (content_reader_ != nullptr) {
                        const auto &content_reader = *content_reader_;

                        FormData    formData;
                        auto        it = formData.fields.begin();

                        content_reader(
                                [&](const httplib::MultipartFormData &file) {
                                    it = formData.fields.emplace(file.name, "").first;
                                    return true;
                                },
                                [&](const char *data, std::size_t data_length) {
                                    // TODO: This should append to content
                                    it->second.append(std::string_view(data, data_length));

                                    return true;
                                });

                        opencmw::IoBuffer buffer;
                        // opencmw::serialise<opencmw::Json>(buffer, formData);
                        IoSerialiser<opencmw::Json, decltype(formData.fields)>::serialise(buffer, FieldDescriptionShort{}, formData.fields);

                        std::string requestData(buffer.asString());
                        // Json serialiser (rightfully) does not like bool values in string
                        static auto replacerRegex = std::regex(R"regex("opencmw_unquoted_value[(](.*)[)]")regex");
                        requestData               = std::regex_replace(requestData, replacerRegex, "$1");
                        auto requestDataBegin     = std::find(requestData.cbegin(), requestData.cend(), '{');

                        message.setBody(std::string_view(requestDataBegin, requestData.cend()), MessageFrame::dynamic_bytes_tag{});
                    }

                } else if (!request.body.empty()) {
                    message.setBody(request.body, MessageFrame::dynamic_bytes_tag{});

                } else {
                    message.setBody(bodyOverride, MessageFrame::dynamic_bytes_tag{});
                }

                if (method == "SUB" || method == "POLL") {
                    respondWithSubscription(method, worker, response, service, topic);

                } else {
                    respondWithPubSub(worker, request, response, message);
                }
            }
        };

        _svr.Get(".*", [defaultHandler](const httplib::Request &request, httplib::Response &response) {
            return defaultHandler(request, response, nullptr);
        });
        _svr.Post(".*", [defaultHandler](const httplib::Request &request, httplib::Response &response, const httplib::ContentReader &content_reader) {
            return defaultHandler(request, response, &content_reader);
        });
        _svr.Put(".*", [defaultHandler](const httplib::Request &request, httplib::Response &response, const httplib::ContentReader &content_reader) {
            return defaultHandler(request, response, &content_reader);
        });
    }

    void respondWithPubSub(auto &worker, auto &request, auto &response, auto &message) {
        if (!message.send(worker.dealer)) {
            respondWithError(response, "Failed to send a message to the broker\n");
            return;
        }

        worker.pollItem.socket = worker.dealer.zmq_ptr;
        if (!zmq_invoke(zmq_poll, &worker.pollItem, 1, std::chrono::duration_cast<std::chrono::milliseconds>(REST_POLLING_TIME).count())) {
            respondWithError(response, "No response from broker\n");
        } else if (auto responseMessage = MdpMessage::receive(worker.dealer); !responseMessage) {
            respondWithError(response, "Empty response from broker\n");
        } else if (!responseMessage->error().empty()) {
            respondWithError(response, responseMessage->error());
        } else {
            response.status = HTTP_OK;

            response.set_header("X-OPENCMW-TOPIC", responseMessage->topic().data());
            response.set_header("X-OPENCMW-SERVICE-NAME", responseMessage->serviceName().data());
            response.set_header("Access-Control-Allow-Origin", "*");

            if (request.method != "GET") {
                response.set_content(responseMessage->body().data(), MIME::TEXT.typeName().data());
            } else {
                const auto acceptedFormat = acceptedMimeForRequest(request);
                response.set_content(responseMessage->body().data(), acceptedFormat.data());
            }
        }
    }

    void respondWithSubscription(const auto &method, auto &worker, auto &response, const auto &service, const auto &topic) {
        std::size_t maxCount = method == "SUB" ? static_cast<std::size_t>(-1) : std::size_t{ 1 };

        (void) service;

        response.set_chunked_content_provider(
                "application/json",
                [this, &worker, topic = topic, service = service, counter = std::size_t{ 0 }, maxCount](std::size_t /*offset*/, httplib::DataSink &sink) mutable {
                    if (!zmq_invoke(zmq_setsockopt, worker.subscriber, ZMQ_SUBSCRIBE, topic.data(), topic.size())) {
                        sink.done();
                        return true;
                    }

                    worker.pollItem.socket = worker.subscriber.zmq_ptr;
                    if (!zmq_invoke(zmq_poll, &worker.pollItem, 1, std::chrono::duration_cast<std::chrono::milliseconds>(REST_POLLING_TIME).count())) {
                        sink.done();

                    } else if (auto responseMessage = BrokerMessage::receive(worker.subscriber); !responseMessage) {
                        sink.done();

                    } else {
                        sink.os << responseMessage->body() << '\n';

                        if (++counter == maxCount) {
                            sink.done();
                        }
                    }
                    return true;
                },
                [this, &worker, topic = topic](bool) {
                    zmq_invoke(zmq_setsockopt, worker.subscriber, ZMQ_UNSUBSCRIBE, topic.data(), topic.size()).isValid();
                });
    }

    void run() {
        thread::setThreadName("RestBackend thread");

        registerHandlers();

        if (!_restAddress.hostName() || !_restAddress.port()) {
            throw opencmw::startup_error(fmt::format("REST server URI is not valid {}", _restAddress.str));
        }

        bool listening = _svr.listen(_restAddress.hostName().value().data(), _restAddress.port().value());
        if (!listening) {
            throw opencmw::startup_error(fmt::format("Can not start REST server on {}:{}", _restAddress.hostName().value().data(), _restAddress.port().value()));
        }
    }

    void requestStop() {
        _svr.stop();
    }
};

} // namespace opencmw::majordomo

#endif // include guard
