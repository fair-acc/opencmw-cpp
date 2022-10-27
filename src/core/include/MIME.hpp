#ifndef OPENCMW_CPP_MIME_HPP
#define OPENCMW_CPP_MIME_HPP

#include <algorithm>
#include <array>
#include <cctype> // for tolower
#include <iostream>
#include <span>
#include <string_view>
#include <vector>
#include <version>

#include <fmt/format.h>

#include <opencmw.hpp>

namespace opencmw {
namespace MIME {

class MimeType {
    std::string_view                _typeName; // TODO: replace with case-insensitive constexpr std::string once fully supported by both gcc13 and clang?, see http://www.gotw.ca/gotw/029.htm
    std::string_view                _description;
    std::array<std::string_view, 3> _fileExtensions; // N.B. max number '3' of ext is hardcoded TODO: replace with constexpr std::array or std::vector once fully supported by both gcc13 and clang?
    size_t                          _N;

public:
    MimeType() = delete;
    template<class... Ts>
    constexpr explicit MimeType(const char *name, const char *description, Ts... ext) noexcept
        : _typeName(name), _description(description), _fileExtensions{ std::forward<Ts>(ext)... }, _N(sizeof...(Ts)) {}
    constexpr std::string_view                  typeName() const noexcept { return _typeName; }
    constexpr std::string_view                  description() const noexcept { return _description; }
    constexpr std::span<const std::string_view> fileExtensions() const noexcept { return std::span(_fileExtensions.data(), _N); };
    constexpr explicit(false)                   operator const char *() const noexcept { return _typeName.data(); }
#if not defined(_LIBCPP_VERSION)
    constexpr explicit(false) operator std::string() const noexcept { return _typeName.data(); }
#endif
    constexpr explicit(false) operator std::string_view() const noexcept { return _typeName; }

#if defined(_LIBCPP_VERSION) and _LIBCPP_VERSION < 16000
    constexpr auto operator<=>(const MimeType &rhs) const noexcept { return _typeName <=> rhs._typeName; }
#endif
    constexpr bool operator==(const MimeType &rhs) const noexcept { return _typeName == rhs._typeName; }
};

/**
 * Definition and convenience methods for common MIME types according to RFC6838 and RFC4855
 * <p>
 * Since the official list is rather long, contains types one may likely never encounter, and also does not contain all
 * unofficial but nevertheless commonly used MIME types, we chose the specific sub-selection from:
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
 */
/* text MIME types */
constexpr MimeType CSS{ "text/css", "Cascading Style Sheets (CSS)", ".css" };
constexpr MimeType CSV{ "text/csv", "Comma-separated values (CSV)", ".csv" };
constexpr MimeType EVENT_STREAM{ "text/event-stream", "SSE stream" };
constexpr MimeType HTML{ "text/html", "HyperText Markup Language (HTML)", ".htm", ".html" };
constexpr MimeType ICS{ "text/calendar", "iCalendar format", ".ics" };
constexpr MimeType JAVASCRIPT{ "text/javascript", "JavaScript", ".js", ".mjs" };
constexpr MimeType JSON{ "application/json", "JSON format", ".json" };
constexpr MimeType JSON_LD{ "application/ld+json", "JSON-LD format", ".jsonld" };
constexpr MimeType TEXT{ "text/plain", "Text, (generally ASCII or ISO 8859-n)", ".txt" };
constexpr MimeType XML{ "text/xml", "XML", ".xml" };                                        // if readable from casual users (RFC 3023, section 3)
constexpr MimeType YAML{ "text/yaml", "YAML Ain't Markup Language File", ".yml", ".yaml" }; // not yet an IANA standard

/* audio MIME types */
constexpr MimeType AAC{ "audio/aac", "AAC audio", ".aac" };
constexpr MimeType MIDI{ "audio/midi", "Musical Instrument Digital Interface (MIDI)", ".mid", ".midi" };
constexpr MimeType MP3{ "audio/mpeg", "MP3 audio", ".mp3" };
constexpr MimeType OTF{ "audio/opus", "Opus audio", ".opus" };
constexpr MimeType WAV{ "audio/wav", "Waveform Audio Format", ".wav" };
constexpr MimeType WEBM_AUDIO{ "audio/webm", "WEBM audio", ".weba" };

/* image MIME types */
constexpr MimeType BMP{ "image/bmp", "Windows OS/2 Bitmap Graphics", ".bmp" };
constexpr MimeType GIF{ "image/gif", "Graphics Interchange Format (GIF)", ".gif" };
constexpr MimeType ICO{ "image/vnd.microsoft.icon", "Icon format", ".ico" };
constexpr MimeType JPEG{ "image/jpeg", "JPEG images", ".jpg", ".jpeg" };
constexpr MimeType PNG{ "image/png", "Portable Network Graphics", ".png" };
constexpr MimeType APNG{ "image/apng", "Portable Network Graphics", ".apng" }; // disabled ambiguous '.png' extension
constexpr MimeType SVG{ "image/svg+xml", "Scalable Vector Graphics (SVG)", ".svg" };
constexpr MimeType TIFF{ "image/tiff", "Tagged Image File Format (TIFF)", ".tif", ".tiff" };
constexpr MimeType WEBP{ "image/webp", "WEBP image", ".webp" };

/* video MIME types */
constexpr MimeType AVI{ "video/x-msvideo", "AVI: Audio Video Interleave", ".avi" };
constexpr MimeType MP2T{ "video/mp2t", "MPEG transport stream", ".ts" };
constexpr MimeType MPEG{ "video/mpeg", "MPEG Video", ".mpeg" };
constexpr MimeType WEBM_VIDEO{ "video/webm", "WEBM video", ".webm" };

/* application-specific audio MIME types -- mostly binary-type formats */
constexpr MimeType BINARY{ "application/octet-stream", "Any kind of binary data", ".bin" };
constexpr MimeType CMWLIGHT{ "application/cmwlight", "proprietary CERN serialiser binary format", ".cmwlight" }; // deprecated: do not use for new projects
// constexpr MimeType BZIP{"application/x-bzip", "BZip archive", ".bz"}; // affected by patent
constexpr MimeType BZIP2{ "application/x-bzip2", "BZip2 archive", ".bz2" };
constexpr MimeType DOC{ "application/msword", "Microsoft Word", ".doc" };
constexpr MimeType DOCX{ "application/vnd.openxmlformats-officedocument.wordprocessingml.document", "Microsoft Word (OpenXML)", ".docx" };
constexpr MimeType GZIP{ "application/gzip", "GZip Compressed Archive", ".gz" };
constexpr MimeType JAR{ "application/java-archive", "Java Archive (JAR)", ".jar" };
constexpr MimeType ODP{ "application/vnd.oasis.opendocument.presentation", "OpenDocument presentation document", ".odp" };
constexpr MimeType ODS{ "application/vnd.oasis.opendocument.spreadsheet", "OpenDocument spreadsheet document", ".ods" };
constexpr MimeType ODT{ "application/vnd.oasis.opendocument.text", "OpenDocument text document", ".odt" };
constexpr MimeType OGG{ "application/ogg", "OGG Audio/Video File", ".ogx", ".ogv", ".oga" };
constexpr MimeType PDF{ "application/pdf", "Adobe Portable Document Format (PDF)", ".pdf" };
constexpr MimeType PHP{ "application/x-httpd-php", "Hypertext Preprocessor (Personal Home Page)", ".php" };
constexpr MimeType PPT{ "application/vnd.ms-powerpoint", "Microsoft PowerPoint", ".ppt" };
constexpr MimeType PPTX{ "application/vnd.openxmlformats-officedocument.presentationml.presentation", "Microsoft PowerPoint (OpenXML)", ".pptx" };
constexpr MimeType RAR{ "application/vnd.rar", "RAR archive", ".rar" };
constexpr MimeType RTF{ "application/rtf", "Rich Text Format (RTF)", ".rtf" };
constexpr MimeType TAR{ "application/x-tar", "Tape Archive (TAR)", ".tar" };
constexpr MimeType VSD{ "application/vnd.visio", "Microsoft Visio", ".vsd" };
constexpr MimeType XHTML{ "application/xhtml+xml", "XHTML", ".xhtml" };
constexpr MimeType XLS{ "application/vnd.ms-excel", "Microsoft Excel", ".xls" };
constexpr MimeType XLSX{ "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "Microsoft Excel (OpenXML)", ".xlsx" };
constexpr MimeType ZIP{ "application/zip", "ZIP archive", ".zip" };
/* fall-back */
constexpr MimeType   UNKNOWN{ "unknown/unknown", "unknown data format" };

constexpr std::array ALL{
    // text MIME types
    CSS, CSV, EVENT_STREAM, HTML, ICS, JAVASCRIPT, JSON, JSON_LD, TEXT, XML, YAML,
    // audio MIME types
    AAC, MIDI, MP3, OTF, WAV, WEBM_AUDIO,
    // image MIME types
    BMP, GIF, ICO, JPEG, PNG, APNG, SVG, TIFF, WEBP,
    // video MIME types
    AVI, MP2T, MPEG, WEBM_VIDEO,
    // application-specific audio MIME types -- mostly binary-type formats
    BINARY, CMWLIGHT, BZIP2, DOC, DOCX, GZIP, JAR, ODP, ODS, ODT, OGG, PDF, PHP, PPT, PPTX, RAR, RTF, TAR, VSD, XHTML, XLS, XLSX, ZIP,
    // unknown category
    UNKNOWN
};

namespace detail {
template<typename T, T Begin, class Func, T... Is>
constexpr void static_for_impl(Func &&f, std::integer_sequence<T, Is...>) {
    (f(std::integral_constant<T, Begin + Is>{}), ...);
}

template<typename T, T Begin, T End, class Func>
constexpr void static_for(Func &&f) {
    static_for_impl<T, Begin>(std::forward<Func>(f), std::make_integer_sequence<T, End - Begin>{});
}

constexpr char toLower(const char c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; } // N.B. needed because std::tolower isn't constexpr
} // namespace detail

/**
 * Case-insensitive mapping between MIME-type string and enumumeration value.
 *
 * @param mimeType the string equivalent mime-type, e.g. "image/png"
 * @return the enumeration equivalent first matching mime-type, e.g. MimeType.PNG or MimeType.UNKNOWN as fall-back
 */
constexpr const MimeType &getType(const std::string_view &mimeType) noexcept {
    if (mimeType.empty()) {
        return UNKNOWN;
    }
    if (std::is_constant_evaluated()) {
        std::size_t    index            = SIZE_MAX;
        constexpr auto lowerCaseCompare = [](const char ch1, const char ch2) constexpr noexcept->bool { return detail::toLower(ch1) == detail::toLower(ch2); };
        detail::static_for<std::size_t, 0, ALL.size()>([&](auto i) {
            constexpr auto type = std::get<i>(ALL);
            if (std::search(mimeType.begin(), mimeType.end(), type.typeName().begin(), type.typeName().end(), lowerCaseCompare) != mimeType.end()) {
                index = i;
            }
        });
        return (index == SIZE_MAX) ? UNKNOWN : ALL[index];
    }

    for (const MimeType &type : ALL) {
        // N.B.mimeType may contain several MIME types, e.g "image/webp,image/apng,image/*"
        constexpr auto lowerCaseCompare = [](const char ch1, const char ch2) noexcept -> bool { return detail::toLower(ch1) == detail::toLower(ch2); };
        if (std::search(mimeType.begin(), mimeType.end(), type.typeName().begin(), type.typeName().end(), lowerCaseCompare) != mimeType.end()) {
            return type;
        }
    }
    return UNKNOWN;
}
static_assert(getType("text/plain") == TEXT);

/**
 * Case-insensitive mapping between MIME-type string and enumeration value.
 *
 * @param fileName the string equivalent mime-type, e.g. "image/png"
 * @return the enumeration equivalent mime-type, e.g. MimeType.PNG or MimeType.UNKNOWN as fall-back
 */
constexpr const MimeType &getTypeByFileName(const std::string_view &fileName) {
    if (fileName.empty()) {
        return UNKNOWN;
    }
    constexpr auto ends_with = [](const std::string_view &value, const std::string_view &ending) constexpr noexcept {
        constexpr auto lowerCaseCompare = [](char ch1, char ch2) constexpr noexcept { return detail::toLower(ch1) == detail::toLower(ch2); };
        return ending.size() <= value.size() ? std::equal(ending.rbegin(), ending.rend(), value.rbegin(), lowerCaseCompare) : false;
    };

    if (std::is_constant_evaluated()) {
        std::size_t index = SIZE_MAX;
        detail::static_for<std::size_t, 0, ALL.size()>([&](auto i) {
            constexpr auto extRange = std::get<i>(ALL).fileExtensions();
            detail::static_for<std::size_t, 0, extRange.size()>([&](auto j) {
                constexpr auto ext = extRange[j];
                if (ends_with(fileName, ext)) {
                    index = i;
                }
            });
        });
        return (index == SIZE_MAX) ? UNKNOWN : ALL[index];
    }

    for (const MimeType &type : ALL) {
        for (const auto &ending : type.fileExtensions()) {
            if (ends_with(fileName, ending)) {
                return type;
            }
        }
    }
    return UNKNOWN;
}
static_assert(getTypeByFileName("TEST.TXT") == TEXT);

} // namespace MIME

template<>
constexpr inline std::string_view typeName<opencmw::MIME::MimeType> = "MimeType";

template<typename T>
inline std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
    os << '[';
    bool first = true;
    for (auto t : v) {
        if (first) {
            os << t;
            first = false;
        } else {
            os << ", ";
            os << t;
        }
    }
    return os << ']';
}

template<typename T>
inline std::ostream &operator<<(std::ostream &os, const std::span<T> &v) {
    os << '[';
    bool first = true;
    for (auto t : v) {
        if (first) {
            os << t;
            first = false;
        } else {
            os << ", ";
            os << t;
        }
    }
    return os << ']';
}

inline std::ostream &operator<<(std::ostream &os, const opencmw::MIME::MimeType &v) {
    return os << v.typeName();
}

} // namespace opencmw

template<>
struct fmt::formatter<opencmw::MIME::MimeType> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin(); // not (yet) implemented
    }

    template<typename FormatContext>
    auto format(opencmw::MIME::MimeType const &v, FormatContext &ctx) const {
        return fmt::format_to(ctx.out(), "{}", v.typeName());
    }
};

#endif // OPENCMW_CPP_MIME_HPP
