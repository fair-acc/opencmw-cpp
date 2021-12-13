#ifndef OPENCMW_MAJORDOMO_MAJORDOMOWORKER_H
#define OPENCMW_MAJORDOMO_MAJORDOMOWORKER_H

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/QueryParameterParser.hpp>

#include <IoSerialiserCmwLight.hpp>
#include <IoSerialiserJson.hpp>
#include <IoSerialiserYaS.hpp>
#include <MIME.hpp>
#include <opencmw.hpp>
#include <Utils.hpp>

#include <fmt/format.h>

namespace opencmw::majordomo {

template<typename T, typename C, typename I, typename O>
concept MajordomoHandler = requires(T t, opencmw::majordomo::RequestContext ctx, C requestCtx, I request, C replyCtx, O reply) {
    { t.handle(ctx, requestCtx, request, replyCtx) } -> std::convertible_to<O>;
};

namespace detail {
    template<ReflectableClass I, typename Protocol>
    inline I deserialiseRequest(const MdpMessage &request) {
        IoBuffer buffer;
        buffer.putRaw(request.body());
        I          input;
        const auto result = opencmw::deserialise<Protocol, opencmw::ProtocolCheck::ALWAYS>(buffer, input);
        if (!result.exceptions.empty()) {
            throw result.exceptions.front();
        }

        return input;
    }

    template<ReflectableClass I>
    inline I deserialiseRequest(const RequestContext &rawCtx) {
        try {
            if (rawCtx.mimeType == MIME::JSON) {
                return deserialiseRequest<I, opencmw::Json>(rawCtx.request);
            } else if (rawCtx.mimeType == MIME::BINARY) {
                return deserialiseRequest<I, opencmw::YaS>(rawCtx.request);
            } else if (rawCtx.mimeType == MIME::CMWLIGHT) {
                // TODO the following line does not compile
                // return deserialiseRequest<I, opencmw::CmwLight>(rawCtx.request);
            }
        } catch (const ProtocolException &e) { // TODO if ProtocolException would inherit from std::exception, we could omit this catch/try and leave it to the generic exception handling in BasicMdpWorker
            throw std::runtime_error(std::string(e.what()));
        }

        throw std::runtime_error(fmt::format("MIME type '{}' not supported", rawCtx.mimeType.typeName()));
    }

    template<typename Protocol>
    inline void serialiseAndWriteToBody(RequestContext & rawCtx, const ReflectableClass auto &output) {
        IoBuffer buffer;
        opencmw::serialise<Protocol>(buffer, output);
        rawCtx.reply.setBody(buffer.asString(), MessageFrame::dynamic_bytes_tag{});
    }

    inline void writeResult(RequestContext & rawCtx, const auto &replyContext, const auto &output) {
        const auto replyQuery = QueryParameterParser::generateQueryParameter(replyContext);
        const auto baseUri    = URI<RELAXED>(std::string(rawCtx.reply.topic().empty() ? rawCtx.request.topic() : rawCtx.reply.topic()));
        const auto topicUri   = URI<RELAXED>::factory(baseUri).queryParam("").build(); // TODO serialise replyQuery map

        rawCtx.reply.setTopic(topicUri.str, MessageFrame::dynamic_bytes_tag{});
        const auto replyMimetype = QueryParameterParser::getMimeType(replyQuery);
        const auto mimeType      = replyMimetype != MIME::UNKNOWN ? replyMimetype : rawCtx.mimeType;
        if (mimeType == MIME::JSON) {
            serialiseAndWriteToBody<opencmw::Json>(rawCtx, output);
            return;
        } else if (mimeType == MIME::BINARY) {
            serialiseAndWriteToBody<opencmw::YaS>(rawCtx, output);
            return;
        } else if (mimeType == MIME::CMWLIGHT) {
            serialiseAndWriteToBody<opencmw::CmwLight>(rawCtx, output);
            return;
        }

        throw std::runtime_error(fmt::format("MIME type '{}' not supported", mimeType.typeName()));
    }

    template<ReflectableClass C, ReflectableClass I, ReflectableClass O, MajordomoHandler<C, I, O> Handler>
    class HandlerImpl {
        Handler _handler;

    public:
        explicit HandlerImpl(Handler handler)
            : _handler(std::forward<Handler>(handler)) {}

        void operator()(RequestContext &rawCtx) {
            using namespace opencmw::MIME;

            const auto reqTopic   = opencmw::URI<RELAXED>(std::string(rawCtx.request.topic()));
            const auto queryMap   = reqTopic.queryParamMap();

            C          requestCtx = QueryParameterParser::parseQueryParameter<C>(queryMap);
            C          replyCtx   = requestCtx;
            // const auto requestedMimeType = QueryParameterParser::getMimeType(queryMap);
            //  no MIME type given -> map default to BINARY
            // rawCtx.mimeType = requestedMimeType == MIME::UNKNOWN ? MIME::BINARY : requestedMimeType;

            const auto input  = deserialiseRequest<I>(rawCtx);
            const auto output = _handler.handle(rawCtx, requestCtx, input, replyCtx);
            writeResult(rawCtx, replyCtx, output);
        }
    };

} // namespace detail

template<ReflectableClass C, ReflectableClass I, ReflectableClass O, MajordomoHandler<C, I, O> UserHandler>
class MajordomoWorker : public BasicMdpWorker<detail::HandlerImpl<C, I, O, UserHandler>> {
public:
    explicit MajordomoWorker(std::string_view serviceName, URI<STRICT> brokerAddress, UserHandler userHandler, const Context &context, Settings settings = {})
        : BasicMdpWorker<detail::HandlerImpl<C, I, O, UserHandler>>(serviceName, std::move(brokerAddress), detail::HandlerImpl<C, I, O, UserHandler>(std::forward<UserHandler>(userHandler)), context, settings) {}

    explicit MajordomoWorker(std::string_view serviceName, const Broker &broker, UserHandler userHandler)
        : BasicMdpWorker<detail::HandlerImpl<C, I, O, UserHandler>>(serviceName, broker, detail::HandlerImpl<C, I, O, UserHandler>(std::forward<UserHandler>(userHandler))) {}

    bool notify(const C &context, const O &reply) {
        return notify("", context, reply);
    }

    // TODO or enforce O for reply?
    bool notify(std::string_view path, const C &context, const ReflectableClass auto &reply) {
        std::ignore = context;

        // Java does _serviceName + path, do we want that?
        // std::string topicString = this->_serviceName;
        // topicString.append(path);
        // auto topicURI = URI<RELAXED>(topicString);

        auto topicURI = URI<RELAXED>(std::string(path));

        // TODO serialize context to query params
        // TODO java does subscription handling here which BasicMdpWorker does in the sender thread...

        RequestContext rawCtx;
        rawCtx.reply.setTopic(topicURI.str, MessageFrame::dynamic_bytes_tag{});
        // TODO
        detail::writeResult(rawCtx, context, reply);
        return BasicMdpWorker<detail::HandlerImpl<C, I, O, UserHandler>>::notify(std::move(rawCtx.reply));
    }
};

} // namespace opencmw::majordomo

#endif
