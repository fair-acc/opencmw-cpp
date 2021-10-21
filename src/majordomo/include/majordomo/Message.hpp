#ifndef MAJORDOMO_OPENCMW_MESSAGE_H
#define MAJORDOMO_OPENCMW_MESSAGE_H

#include <yaz/yaz.hpp>

#include <cassert>
#include <optional>
#include <vector>

namespace Majordomo::OpenCMW {
    using Bytes = yaz::Bytes;

    class Message {
        static constexpr auto clientProtocol = "MDPC03";
        static constexpr auto workerProtocol = "MDPW03";

        static constexpr auto numFrames = 9;
        std::vector<Bytes> _frames{numFrames};

        enum class Frame : std::size_t {
            SourceId=0,
            Protocol=1,
            Command=2,
            ServiceName=3,
            ClientSourceId=ServiceName,
            ClientRequestId=4,
            Topic=5,
            Body=6,
            Error=7,
            RBAC=8
        };

        template<typename T>
        static constexpr auto index(T value) {
            return static_cast<std::underlying_type_t<T>>(value);
        }

        explicit Message(std::vector<Bytes> &&frames)
            : _frames(std::move(frames))
        {}

        explicit Message(char command) {
            _frames[index(Frame::Command)] += command;
        }

        char command() const {
            assert(_frames[index(Frame::Command)].length() == 1);
            return static_cast<char>(_frames[index(Frame::Command)][0]);
        }

        // TODO better error handling
        static bool checkYazMessage(const yaz::Message &ymsg) {
            // TODO better error reporting
            if (ymsg.parts_count() != numFrames) {
                return false;
            }

            const auto commandStr = ymsg[index(Frame::Command)];

            if (commandStr.size() != 1) {
                return false;
            }

            const auto protocol = ymsg[index(Frame::Protocol)];
            const auto command = static_cast<unsigned char>(commandStr[0]);
            if (protocol == clientProtocol) {
                if (command == 0 || command > 0x06) { // Final
                    return false;
                }

            } else if (protocol == workerProtocol) {
                if (command == 0 || command > 0x08) { // Hearbeat
                    return false;
                }
            } else {
                return false;
            }

            return true;
        }

    public:
        enum class ClientCommand {
            Get=0x01,
            Set=0x02,
            Subscribe=0x03,
            Unsubscribe=0x04,
            Partial=0x05,
            Final=0x06
        };

        enum class WorkerCommand {
            Get=0x01,
            Set=0x02,
            Partial=0x03,
            Final=0x04,
            Notify=0x05,
            Ready=0x06,
            Disconnect=0x07,
            Heartbeat=0x08
        };

        enum class Protocol {
            Client,
            Worker
        };

        ~Message() = default;
        Message(const Message &) = delete;
        Message &operator=(const Message &) = delete;
        Message(Message &&other) = default;
        Message &operator=(Message &&other) = default;

        static Message createClientMessage(ClientCommand cmd) {
            Message msg{static_cast<char>(cmd)};
            msg._frames[index(Frame::Protocol)] = clientProtocol;
            return msg;
        }

        static Message createWorkerMessage(WorkerCommand cmd) {
            Message msg{static_cast<char>(cmd)};
            msg._frames[index(Frame::Protocol)] = workerProtocol;
            return msg;
        }

        Protocol protocol() const {
            const auto protocol = _frames[index(Frame::Protocol)];
            assert(protocol == clientProtocol || protocol == workerProtocol);
            return protocol == clientProtocol ? Protocol::Client : Protocol::Worker;
        }

        bool isClientMessage() const {
            return protocol() == Protocol::Client;
        }

        bool isWorkerMessage() const {
            return protocol() == Protocol::Worker;
        }

        ClientCommand clientCommand() const {
            assert(isClientMessage());
            return static_cast<ClientCommand>(command());
        }

        WorkerCommand workerCommand() const {
            assert(isWorkerMessage());
            return static_cast<WorkerCommand>(command());
        }

        template <typename T>
        void setServiceName(T serviceName) {
            _frames[index(Frame::ServiceName)] = std::move(serviceName);
        }

        std::string_view serviceName() const {
            return _frames[index(Frame::ServiceName)];
        }

        template <typename T>
        void setClientSourceId(T clientSourceId) {
            _frames[index(Frame::ClientSourceId)] = std::move(clientSourceId);
        }

        std::string_view clientSourceId() const {
            return _frames[index(Frame::ClientSourceId)];
        }

        template <typename T>
        void setClientRequestId(T clientRequestId) {
            _frames[index(Frame::ClientRequestId)] = std::move(clientRequestId);
        }

        std::string_view clientRequestId() const {
            return _frames[index(Frame::ClientRequestId)];
        }

        template <typename T>
        void setTopic(T topic) {
            _frames[index(Frame::Topic)] = std::move(topic);
        }

        std::string_view topic() const {
            return _frames[index(Frame::Topic)];
        }

        template <typename T>
        void setBody(T body) {
            _frames[index(Frame::Body)] = std::move(body);
        }

        std::string_view body() const {
            return _frames[index(Frame::Body)];
        }

        template <typename T>
        void setError(T error) {
            _frames[index(Frame::Error)] = std::move(error);
        }

        std::string_view error() const {
            return _frames[index(Frame::Error)];
        }

        template <typename T>
        void setRbac(T rbac) {
            _frames[index(Frame::RBAC)] = std::move(rbac);
        }

        std::string_view rbac() const {
            return _frames[index(Frame::RBAC)];
        }

        std::vector<Bytes> takeFrames() {
            return std::move(_frames);
        }

        static yaz::Message toYazMessage(Message &&msg) {
            // TODO assert validity?
            return yaz::Message(std::move(msg._frames));
        }

        static std::optional<Message> fromYazMessage(yaz::Message &&ymsg) {
            // TODO report error details
            if (!checkYazMessage(ymsg))
                return std::optional<Message>();

            return Message(std::move(ymsg.take_parts()));
       }

    };

    static_assert(std::is_nothrow_move_constructible<Message>::value, "Message should be noexcept MoveConstructible");
}

#endif
