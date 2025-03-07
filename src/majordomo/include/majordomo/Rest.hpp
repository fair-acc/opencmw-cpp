#ifndef OPENCMW_MAJORDOMO_REST_HPP
#define OPENCMW_MAJORDOMO_REST_HPP

#include <IoBuffer.hpp>

#include <cmrc/cmrc.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <string>
#include <vector>

namespace opencmw::majordomo::rest {

    struct Request {
        std::string                                      method;
        std::string                                      path;
        std::vector<std::pair<std::string, std::string>> headers;
    };

    struct Response {
        struct ReadStatus {
            std::size_t bytesWritten;
            bool        hasMore;
        };
        using WriterFunction = std::function<std::expected<ReadStatus, std::string>(std::span<std::uint8_t>)>;
        int                                              code;
        std::vector<std::pair<std::string, std::string>> headers;
        WriterFunction                                   bodyReader;
        IoBuffer                                         body;
    };

    struct Handler {
        std::string                              method;
        std::string                              path;
        std::function<Response(const Request &)> handler;
    };

    inline auto mimeTypeFromExtension(std::string_view path) {
        if (path.ends_with(".html")) {
            return "text/html";
        }
        if (path.ends_with(".css")) {
            return "text/css";
        }
        if (path.ends_with(".js")) {
            return "application/javascript";
        }
        if (path.ends_with(".png")) {
            return "image/png";
        }
        if (path.ends_with(".jpg") || path.ends_with(".jpeg")) {
            return "image/jpeg";
        }
        if (path.ends_with(".wasm")) {
            return "application/wasm";
        }
        return "text/plain";
    }

    namespace detail {
    struct CmrcReadState {
        cmrc::file  file;
        std::size_t pos = 0;
    };

    } // namespace detail

    inline auto cmrcHandler(std::string path, std::string prefix, std::shared_ptr<cmrc::embedded_filesystem> vfs, std::string vprefix) {
        return Handler{
            .method  = "GET",
            .path    = path,
            .handler = [path, prefix, vfs, vprefix](const Request &request) -> Response {
                try {
                    auto p = std::string_view{ request.path };
                    if (p.starts_with(prefix)) {
                        p.remove_prefix(prefix.size());
                    }
                    while (p.starts_with("/")) {
                        p.remove_prefix(1);
                    }
                    auto     state = std::make_shared<detail::CmrcReadState>(vfs->open(vprefix + std::string{ p }), 0);
                    Response response;
                    response.code = 200;
                    response.headers.emplace_back("content-type", mimeTypeFromExtension(p));
                    response.headers.emplace_back("content-length", std::to_string(state->file.size()));
                    response.bodyReader = [state = std::move(state)](std::span<std::uint8_t> buffer) -> std::expected<Response::ReadStatus, std::string> {
                        const std::size_t n = std::min(buffer.size(), state->file.size() - state->pos);
                        std::copy(state->file.begin() + state->pos, state->file.begin() + state->pos + n, buffer.begin());
                        state->pos += n;
                        return Response::ReadStatus{ n, state->pos < state->file.size() };
                    };
                    return response;
                } catch (...) {
                    Response response;
                    response.code = 404;
                    response.headers.emplace_back("content-type", "text/plain");
                    response.body = IoBuffer("Not found");
                    return response;
                }
            }
        };
    }

    inline auto fileSystemHandler(std::string path, std::string prefix, std::filesystem::path root, std::vector<std::pair<std::string, std::string>> extraHeaders = {}) {
        return Handler{
            .method  = "GET",
            .path    = path,
            .handler = [root, path, prefix, extraHeaders = std::move(extraHeaders)](const Request &request) -> Response {
                auto p = std::string_view{ request.path };
                if (p.starts_with(prefix)) {
                    p.remove_prefix(prefix.size());
                }
                while (p.starts_with("/")) {
                    p.remove_prefix(1);
                }
                auto file = root / p;
                if (!std::filesystem::exists(file)) {
                    Response response;
                    response.code = 404;
                    response.headers.emplace_back("content-type", "text/plain");
                    response.body = IoBuffer("Not found");
                    return response;
                }

                try {
                    auto fileStream = std::make_shared<std::ifstream>(file, std::ios::binary);

                    if (!fileStream->is_open()) {
                        Response response;
                        response.code = 500;
                        response.headers.emplace_back("content-type", "text/plain");
                        response.body = IoBuffer("Internal Server Error");
                        return response;
                    }

                    Response response;
                    response.code    = 200;
                    response.headers = extraHeaders;
                    response.headers.emplace_back("content-type", mimeTypeFromExtension(request.path));
                    response.headers.emplace_back("content-length", std::to_string(std::filesystem::file_size(file)));
                    response.bodyReader = [fileStream = std::move(fileStream)](std::span<std::uint8_t> buffer) -> std::expected<Response::ReadStatus, std::string> {
                        fileStream->read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                        if (fileStream->bad()) {
                            return std::unexpected(std::format("Failed to read file: {}", strerror(errno)));
                        }
                        return Response::ReadStatus{ static_cast<std::size_t>(fileStream->gcount()), !fileStream->eof() };
                    };
                    return response;
                } catch (...) {
                    Response response;
                    response.code = 500;
                    response.headers.emplace_back("content-type", "text/plain");
                    response.body = IoBuffer("Internal Server Error");
                    return response;
                }
            }
        };
    }

struct Settings {
    uint16_t                   port = 8080;
    std::filesystem::path      certificateFilePath;
    std::filesystem::path      keyFilePath;
    std::string                certificateFileBuffer;
    std::string                keyFileBuffer;
    std::string                dnsAddress;
    std::vector<rest::Handler> handlers;
};

} // namespace opencmw::majordomo::rest

#endif
