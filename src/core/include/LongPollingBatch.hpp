#ifndef OPENCMW_LONGPOLLINGBATCH_HPP
#define OPENCMW_LONGPOLLINGBATCH_HPP

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opencmw::long_polling {

inline constexpr std::string_view kIndexParameter = "LongPollingIdx";
inline constexpr std::string_view kBatchParameter = "LongPollingBatch";
inline constexpr std::string_view kAllAvailable   = "AllAvailable";
// Payloads may contain a line matching this separator, so read each part by its content-length;
// a generic MIME parser scanning for delimiter lines can mistake such a line for a part's end.
inline constexpr std::string_view kBoundary       = "opencmw-long-polling-multipart-boundary";

struct BatchPart {
    std::uint64_t    index;
    std::string_view topic;
    std::string_view serviceName;
    std::string_view payload;
};

inline std::expected<std::uint64_t, std::string> parseUnsigned(std::string_view value, std::string_view name) {
    if (value.empty()) {
        return std::unexpected(std::string("Malformed ") + std::string(name) + " ''");
    }

    std::uint64_t result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return std::unexpected(std::string("Malformed ") + std::string(name) + " '" + std::string(value) + "'");
    }
    return result;
}

inline std::expected<std::string, std::string> encodeBatch(const std::vector<BatchPart> &parts) {
    for (const auto &part : parts) {
        if (part.topic.find_first_of("\r\n") != std::string_view::npos || part.serviceName.find_first_of("\r\n") != std::string_view::npos) {
            return std::unexpected("Multipart topic and service name must not contain CR or LF");
        }
    }

    std::string body;
    for (const auto &[index, topic, serviceName, payload] : parts) {
        body += "--";
        body += kBoundary;
        body += "\r\nx-opencmw-long-polling-idx: ";
        body += std::to_string(index);
        body += "\r\nx-opencmw-topic: ";
        body += topic;
        body += "\r\nx-opencmw-service-name: ";
        body += serviceName;
        body += "\r\ncontent-length: ";
        body += std::to_string(payload.size());
        body += "\r\n\r\n";
        if (!payload.empty()) {
            body.append(payload.data(), payload.size());
        }
        body += "\r\n";
    }
    body += "--";
    body += kBoundary;
    body += "--\r\n";
    return body;
}

inline std::expected<std::vector<BatchPart>, std::string> decodeBatch(std::string_view body) {
    const auto boundaryEnd = body.find("\r\n");
    if (boundaryEnd == std::string_view::npos || !body.starts_with("--")) {
        return std::unexpected("Missing multipart boundary");
    }
    const auto boundaryLine = body.substr(0, boundaryEnd);
    if (boundaryLine.size() <= 2 || boundaryLine.size() > 72) {
        return std::unexpected("Invalid multipart boundary length");
    }
    const std::string      boundary(boundaryLine);
    const std::string      partBoundary  = boundary + "\r\n";
    const std::string      finalBoundary = boundary + "--\r\n";
    std::vector<BatchPart> parts;
    std::size_t            position = 0;

    while (position < body.size()) {
        if (body.substr(position).starts_with(finalBoundary)) {
            position += finalBoundary.size();
            if (position != body.size()) {
                return std::unexpected("Unexpected data after final multipart boundary");
            }
            return parts;
        }
        if (!body.substr(position).starts_with(partBoundary)) {
            return std::unexpected("Missing multipart boundary");
        }
        position += partBoundary.size();

        const auto headerEnd = body.find("\r\n\r\n", position);
        if (headerEnd == std::string_view::npos) {
            return std::unexpected("Incomplete multipart headers");
        }

        std::optional<std::uint64_t>    index;
        std::optional<std::uint64_t>    contentLength;
        std::optional<std::string_view> topic;
        std::optional<std::string_view> serviceName;
        auto                            headerPosition = position;
        while (headerPosition < headerEnd) {
            const auto lineEnd = body.find("\r\n", headerPosition);
            const auto end     = std::min(lineEnd == std::string_view::npos ? headerEnd : lineEnd, headerEnd);
            const auto line    = body.substr(headerPosition, end - headerPosition);
            if (line.starts_with("x-opencmw-long-polling-idx: ")) {
                auto parsed = parseUnsigned(line.substr(std::string_view("x-opencmw-long-polling-idx: ").size()), "multipart index");
                if (!parsed.has_value()) {
                    return std::unexpected(parsed.error());
                }
                index = *parsed;
            } else if (line.starts_with("x-opencmw-topic: ")) {
                topic = line.substr(std::string_view("x-opencmw-topic: ").size());
            } else if (line.starts_with("x-opencmw-service-name: ")) {
                serviceName = line.substr(std::string_view("x-opencmw-service-name: ").size());
            } else if (line.starts_with("content-length: ")) {
                auto parsed = parseUnsigned(line.substr(std::string_view("content-length: ").size()), "multipart content length");
                if (!parsed.has_value()) {
                    return std::unexpected(parsed.error());
                }
                contentLength = *parsed;
            }
            headerPosition = end + 2;
        }
        if (!index.has_value() || !topic.has_value() || !serviceName.has_value() || !contentLength.has_value()) {
            return std::unexpected("Multipart part is missing its index, topic, service name, or content length");
        }

        position = headerEnd + 4;
        if (*contentLength > body.size() - position) {
            return std::unexpected("Multipart payload is shorter than its content length");
        }
        if (!parts.empty() && (parts.back().index == std::numeric_limits<std::uint64_t>::max() || *index != parts.back().index + 1)) {
            return std::unexpected("Long-polling batch contains non-consecutive indices");
        }
        const auto payloadSize = static_cast<std::size_t>(*contentLength);
        parts.push_back(BatchPart{ *index, *topic, *serviceName, body.substr(position, payloadSize) });
        position += payloadSize;
        if (!body.substr(position).starts_with("\r\n")) {
            return std::unexpected("Multipart payload is not followed by a boundary");
        }
        position += 2;
    }

    return std::unexpected("Missing final multipart boundary");
}

} // namespace opencmw::long_polling

#endif // OPENCMW_LONGPOLLINGBATCH_HPP
