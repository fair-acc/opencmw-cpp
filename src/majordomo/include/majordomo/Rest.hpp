#ifndef OPENCMW_MAJORDOMO_REST_HPP
#define OPENCMW_MAJORDOMO_REST_HPP

#include <IoBuffer.hpp>

#include <cmrc/cmrc.hpp>

#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
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
    // To provide a body, set one of the following:
    WriterFunction bodyReader;
    IoBuffer       body; //< owned data
    // Data for the body not owned by the view. Handler must ensure lifetime
    // (Note that the IoBuffer API allows non-owning cases, but they fail when the buffer is moved)
    std::span<const std::uint8_t> bodyView;
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
                auto     file = vfs->open(vprefix + std::string{ p });
                Response response;
                response.code = 200;
                response.headers.emplace_back("content-type", mimeTypeFromExtension(p));
                response.headers.emplace_back("content-length", std::to_string(file.size()));
                response.body = IoBuffer(file.begin(), file.size());
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

namespace detail {
struct MappedFile {
    int                                           _fd   = -1;
    size_t                                        _size = 0;
    std::uint8_t                                 *_data = nullptr;

    static std::expected<MappedFile, std::string> map(const std::filesystem::path &path) {
        MappedFile f;
        f._fd = open(path.c_str(), O_RDONLY);
        if (f._fd == -1) {
            return std::unexpected(strerror(errno));
        }
        f._size = std::filesystem::file_size(path);
        f._data = static_cast<std::uint8_t *>(mmap(nullptr, f._size, PROT_READ, MAP_PRIVATE, f._fd, 0));
        if (f._data == MAP_FAILED) {
            return std::unexpected(strerror(errno));
        }
        return f;
    }

    MappedFile() = default;

    ~MappedFile() {
        if (_data && _data != MAP_FAILED) {
            munmap(_data, _size);
        }
        if (_fd != -1) {
            close(_fd);
        }
    }

    MappedFile(const MappedFile &)            = delete;
    MappedFile &operator=(const MappedFile &) = delete;
    MappedFile(MappedFile &&other) noexcept {
        if (_fd != -1) {
            munmap(_data, _size);
            close(_fd);
        }
        _fd   = std::exchange(other._fd, -1);
        _size = std::exchange(other._size, 0);
        _data = std::exchange(other._data, nullptr);
    }

    MappedFile &operator=(MappedFile &&other) noexcept {
        if (this != &other) {
            if (_fd != -1) {
                munmap(_data, _size);
                close(_fd);
            }
            _fd   = std::exchange(other._fd, -1);
            _size = std::exchange(other._size, 0);
            _data = std::exchange(other._data, nullptr);
        }
        return *this;
    }

    std::span<std::uint8_t> view() const {
        return { _data, _size };
    }

    size_t size() const {
        return _size;
    }
};

} // namespace detail

inline auto fileSystemHandler(std::string path, std::string prefix, std::filesystem::path root, std::vector<std::pair<std::string, std::string>> extraHeaders = {}, std::size_t mmapThreshold = 1024 * 1024 * 100) {
    return Handler{
        .method  = "GET",
        .path    = path,
        .handler = [mappedFiles = std::make_shared<std::map<std::filesystem::path, detail::MappedFile>>(), root, path, prefix, extraHeaders = std::move(extraHeaders), mmapThreshold](const Request &request) -> Response {
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

            // mmap if file is large enough
            if (std::filesystem::file_size(file) >= mmapThreshold) {
                auto it = mappedFiles->find(file);

                if (it == mappedFiles->end()) {
                    auto mapped = detail::MappedFile::map(file);
                    if (!mapped) {
                        auto    &error = mapped.error();
                        Response response;
                        response.code = 500;
                        response.headers.emplace_back("content-type", "text/plain");
                        response.headers.emplace_back("content-length", std::to_string(error.size()));
                        response.body = IoBuffer(error.data(), error.size());
                        return response;
                    }
                    it = mappedFiles->emplace(file, std::move(mapped.value())).first;
                }

                Response response;
                response.code = 200;
                response.headers.emplace_back("content-type", mimeTypeFromExtension(file.string()));
                auto view = it->second.view();
                response.headers.emplace_back("content-length", std::to_string(view.size()));
                response.bodyView = view;
                return response;
            }

            // otherwise use ifstream
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

enum Protocol {
    Http2 = 0x1,
    Http3 = 0x2,
};

struct Settings {
    uint16_t                   port = 8080;
    std::filesystem::path      certificateFilePath;
    std::filesystem::path      keyFilePath;
    std::string                certificateFileBuffer;
    std::string                keyFileBuffer;
    std::string                dnsAddress;
    std::vector<rest::Handler> handlers;
    int                        protocols = Protocol::Http2 | Protocol::Http3;
};

} // namespace opencmw::majordomo::rest

#endif
