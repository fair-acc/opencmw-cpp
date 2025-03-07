#ifndef OPENCMW_MAJORDOMO_REST_HPP
#define OPENCMW_MAJORDOMO_REST_HPP

#include <IoBuffer.hpp>

#include <cmrc/cmrc.hpp>

#include <expected>
#include <filesystem>
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
        int                                              code;
        std::vector<std::pair<std::string, std::string>> headers;
        std::function<std::expected<std::size_t, std::string>(std::span<std::uint8_t>)> bodyReader;
        IoBuffer                                                                        body;
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
        return "text/plain";
    }

    namespace detail {
    struct CmrcReadState {
        cmrc::file  file;
        std::size_t pos = 0;
    };

    } // namespace detail

    inline auto cmrcHandler(std::string path, std::shared_ptr<cmrc::embedded_filesystem> fs, std::string prefix) {
        return Handler{
            .method  = "GET",
            .path    = path,
            .handler = [fs, path, prefix](const Request &request) -> Response {
                try {
                    auto p = std::string_view{ request.path };
                    p.remove_prefix(path.ends_with("*") ? path.size() - 1 : path.size());
                    while (p.starts_with("/")) {
                        p.remove_prefix(1);
                    }
                    auto     state = std::make_shared<detail::CmrcReadState>(fs->open(prefix + std::string{ p }), 0);
                    Response response;
                    response.code = 200;
                    response.headers.emplace_back("content-type", mimeTypeFromExtension(p));
                    response.bodyReader = [state = std::move(state)](std::span<std::uint8_t> buffer) -> std::expected<std::size_t, std::string> {
                        const std::size_t n = std::min(buffer.size(), state->file.size() - state->pos);
                        std::copy(state->file.begin() + state->pos, state->file.begin() + state->pos + n, buffer.begin());
                        state->pos += n;
                        return n;
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

    inline auto fileSystemHandler(std::string path, std::filesystem::path root, std::vector<std::pair<std::string, std::string>> extraHeaders = {}) {
        return Handler{
            .method  = "GET",
            .path    = path,
            .handler = [root, path, extraHeaders = std::move(extraHeaders)](const Request &request) -> Response {
                auto p = std::string_view{ request.path };
                p.remove_prefix(path.ends_with("*") ? path.size() - 1 : path.size());
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
                    response.bodyReader = [fileStream = std::move(fileStream)](std::span<std::uint8_t> buffer) -> std::expected<std::size_t, std::string> {
                        if (fileStream->eof()) {
                            return 0;
                        }

                        fileStream->read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                        if (fileStream->bad()) {
                            return std::unexpected(fmt::format("Failed to read file: {}", strerror(errno)));
                        }

                        return static_cast<std::size_t>(fileStream->gcount());
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
    std::chrono::milliseconds  majordomoTimeout = std::chrono::seconds(30);
};

} // namespace opencmw::majordomo::rest

#endif
