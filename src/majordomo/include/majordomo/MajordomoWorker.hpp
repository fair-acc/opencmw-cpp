#ifndef OPENCMW_MAJORDOMO_MAJORDOMOWORKER_H
#define OPENCMW_MAJORDOMO_MAJORDOMOWORKER_H

#include <majordomo/BasicMdpWorker.hpp>
#include <majordomo/QuerySerialiser.hpp>

#include <IoSerialiserCmwLight.hpp>
#include <IoSerialiserJson.hpp>
#include <IoSerialiserYaS.hpp>
#include <MIME.hpp>
#include <opencmw.hpp>
#include <Utils.hpp>

#include <fmt/format.h>

namespace opencmw::majordomo {

template<typename T, typename ContextType, typename InputType, typename OutputType>
concept MajordomoHandler = isReflectableClass<ContextType>() && isReflectableClass<InputType>() && isReflectableClass<OutputType>() && requires(T t, opencmw::majordomo::RequestContext ctx, const ContextType &requestCtx, const InputType &request, ContextType &replyCtx, OutputType &reply) {
    t.handle(ctx, requestCtx, request, replyCtx, reply);
};

namespace detail {
    template<ReflectableClass I, typename Protocol>
    inline I deserialiseRequest(const MdpMessage &request) {
        IoBuffer buffer;
        buffer.put<IoBuffer::MetaInfo::WITHOUT>(request.body());
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
        auto       replyQuery = query::serialise(replyContext);
        const auto baseUri    = URI<RELAXED>(std::string(rawCtx.reply.topic().empty() ? rawCtx.request.topic() : rawCtx.reply.topic()));
        const auto topicUri   = URI<RELAXED>::factory(baseUri).setQuery(std::move(replyQuery)).build();

        rawCtx.reply.setTopic(topicUri.str, MessageFrame::dynamic_bytes_tag{});
        const auto replyMimetype = query::getMimeType(replyContext);
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

    template<ReflectableClass ContextType, ReflectableClass InputType, ReflectableClass OutputType, MajordomoHandler<ContextType, InputType, OutputType> Handler>
    class HandlerImpl {
        Handler _handler;

    public:
        explicit HandlerImpl(Handler handler)
            : _handler(std::forward<Handler>(handler)) {}

        void operator()(RequestContext &rawCtx) {
            const auto  reqTopic          = opencmw::URI<RELAXED>(std::string(rawCtx.request.topic()));
            const auto  queryMap          = reqTopic.queryParamMap();

            ContextType requestCtx        = query::deserialise<ContextType>(queryMap);
            ContextType replyCtx          = requestCtx;
            const auto  requestedMimeType = query::getMimeType(requestCtx);
            //  no MIME type given -> map default to BINARY
            rawCtx.mimeType  = requestedMimeType == MIME::UNKNOWN ? MIME::BINARY : requestedMimeType;

            const auto input = deserialiseRequest<InputType>(rawCtx);

            OutputType output;
            _handler.handle(rawCtx, requestCtx, input, replyCtx, output);
            writeResult(rawCtx, replyCtx, output);
        }
    };

} // namespace detail

// TODO docs, see majordomoworker_tests.cpp for a documented example
template<ReflectableClass ContextType, ReflectableClass InputType, ReflectableClass OutputType, MajordomoHandler<ContextType, InputType, OutputType> UserHandler, rbac::role... Roles>
class MajordomoWorker : public BasicMdpWorker<detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>, Roles...> {
public:
    explicit MajordomoWorker(std::string_view serviceName, URI<STRICT> brokerAddress, UserHandler userHandler, const Context &context, Settings settings = {})
        : BasicMdpWorker<detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler, Roles...>>(serviceName, std::move(brokerAddress), detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>(std::forward<UserHandler>(userHandler)), context, settings) {
        query::registerTypes(ContextType(), *this);
    }

    template<typename BrokerType>
    explicit MajordomoWorker(std::string_view serviceName, const BrokerType &broker, UserHandler userHandler)
        : BasicMdpWorker<detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>, Roles...>(serviceName, broker, detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>(std::forward<UserHandler>(userHandler))) {
        query::registerTypes(ContextType(), *this);
    }

    bool notify(const ContextType &context, const OutputType &reply) {
        return notify("", context, reply);
    }

    bool notify(std::string_view path, const ContextType &context, const OutputType &reply) {
        // Java does _serviceName + path, do we want that?
        // std::string topicString = this->_serviceName;
        // topicString.append(path);
        // auto topicURI = URI<RELAXED>(topicString);

        auto       query    = query::serialise(context);
        const auto topicURI = URI<RELAXED>::factory(URI<RELAXED>(std::string(path))).setQuery(std::move(query)).build();

        // TODO java does subscription handling here which BasicMdpWorker does in the sender thread. check what we need there.

        RequestContext rawCtx;
        rawCtx.reply.setTopic(topicURI.str, MessageFrame::dynamic_bytes_tag{});
        detail::writeResult(rawCtx, context, reply);
        return BasicMdpWorker<detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>, Roles...>::notify(std::move(rawCtx.reply));
    }
};

} // namespace opencmw::majordomo

#endif
