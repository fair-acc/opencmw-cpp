#ifndef OPENCMW_CPP_MIME_HPP
#define OPENCMW_CPP_MIME_HPP

#include <algorithm>
#include <array>
#include <cctype> // for tolower
#include <iostream>
#include <string_view>
#include <vector>

namespace opencmw::MIME {

class MimeType {
    static constexpr std::string_view   space = " ";
    const std::string_view              _typeName;
    const std::string_view              _description;
    const std::vector<std::string_view> _fileExtensions;

public:
    MimeType() = delete;
    template<class... Ts>
    constexpr explicit MimeType(const char *name, const char *description, Ts... ext) noexcept
        : _typeName(name), _description(description), _fileExtensions{ std::forward<Ts>(ext)... } {}
    const std::string_view              typeName() const noexcept { return _typeName; }
    const std::string_view              description() const noexcept { return _description; }
    const std::vector<std::string_view> fileExtensions() const noexcept { return _fileExtensions; }

    constexpr auto operator<=>(const MimeType &rhs) const noexcept { return _typeName <=> rhs._typeName; }
    constexpr bool operator!=(const MimeType &rhs) const noexcept { return _typeName != rhs._typeName; }
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
static const MimeType CSS{ "text/css", "Cascading Style Sheets (CSS)", ".css" };
static const MimeType CSV{ "text/csv", "Comma-separated values (CSV)", ".csv" };
static const MimeType EVENT_STREAM{ "text/event-stream", "SSE stream" };
static const MimeType HTML{ "text/html", "HyperText Markup Language (HTML)", ".htm", ".html" };
static const MimeType ICS{ "text/calendar", "iCalendar format", ".ics" };
static const MimeType JAVASCRIPT{ "text/javascript", "JavaScript", ".js", ".mjs" };
static const MimeType JSON{ "application/json", "JSON format", ".json" };
static const MimeType JSON_LD{ "application/ld+json", "JSON-LD format", ".jsonld" };
static const MimeType TEXT{ "text/plain", "Text, (generally ASCII or ISO 8859-n)", ".txt" };
static const MimeType XML{ "text/xml", "XML", ".xml" };                                        // if readable from casual users (RFC 3023, section 3)
static const MimeType YAML{ "text/yaml", "YAML Ain't Markup Language File", ".yml", ".yaml" }; // not yet an IANA standard

/* audio MIME types */
static const MimeType AAC{ "audio/aac", "AAC audio", ".aac" };
static const MimeType MIDI{ "audio/midi", "Musical Instrument Digital Interface (MIDI)", ".mid", ".midi" };
static const MimeType MP3{ "audio/mpeg", "MP3 audio", ".mp3" };
static const MimeType OTF{ "audio/opus", "Opus audio", ".opus" };
static const MimeType WAV{ "audio/wav", "Waveform Audio Format", ".wav" };
static const MimeType WEBM_AUDIO{ "audio/webm", "WEBM audio", ".weba" };

/* image MIME types */
static const MimeType BMP{ "image/bmp", "Windows OS/2 Bitmap Graphics", ".bmp" };
static const MimeType GIF{ "image/gif", "Graphics Interchange Format (GIF)", ".gif" };
static const MimeType ICO{ "image/vnd.microsoft.icon", "Icon format", ".ico" };
static const MimeType JPEG{ "image/jpeg", "JPEG images", ".jpg", ".jpeg" };
static const MimeType PNG{ "image/png", "Portable Network Graphics", ".png" };
static const MimeType APNG{ "image/apng", "Portable Network Graphics", ".apng" }; // disabled ambiguous '.png' extension
static const MimeType SVG{ "image/svg+xml", "Scalable Vector Graphics (SVG)", ".svg" };
static const MimeType TIFF{ "image/tiff", "Tagged Image File Format (TIFF)", ".tif", ".tiff" };
static const MimeType WEBP{ "image/webp", "WEBP image", ".webp" };

/* video MIME types */
static const MimeType AVI{ "video/x-msvideo", "AVI: Audio Video Interleave", ".avi" };
static const MimeType MP2T{ "video/mp2t", "MPEG transport stream", ".ts" };
static const MimeType MPEG{ "video/mpeg", "MPEG Video", ".mpeg" };
static const MimeType WEBM_VIDEO{ "video/webm", "WEBM video", ".webm" };

/* application-specific audio MIME types -- mostly binary-type formats */
static const MimeType BINARY{ "application/octet-stream", "Any kind of binary data", ".bin" };
static const MimeType CMWLIGHT{ "application/cmwlight", "proprietary CERN serialiser binary format", ".cmwlight" }; // deprecated: do not use for new projects
// static const MimeType BZIP{"application/x-bzip", "BZip archive", ".bz"}; // affected by patent
static const MimeType BZIP2{ "application/x-bzip2", "BZip2 archive", ".bz2" };
static const MimeType DOC{ "application/msword", "Microsoft Word", ".doc" };
static const MimeType DOCX{ "application/vnd.openxmlformats-officedocument.wordprocessingml.document", "Microsoft Word (OpenXML)", ".docx" };
static const MimeType GZIP{ "application/gzip", "GZip Compressed Archive", ".gz" };
static const MimeType JAR{ "application/java-archive", "Java Archive (JAR)", ".jar" };
static const MimeType ODP{ "application/vnd.oasis.opendocument.presentation", "OpenDocument presentation document", ".odp" };
static const MimeType ODS{ "application/vnd.oasis.opendocument.spreadsheet", "OpenDocument spreadsheet document", ".ods" };
static const MimeType ODT{ "application/vnd.oasis.opendocument.text", "OpenDocument text document", ".odt" };
static const MimeType OGG{ "application/ogg", "OGG Audio/Video File", ".ogx", ".ogv", ".oga" };
static const MimeType PDF{ "application/pdf", "Adobe Portable Document Format (PDF)", ".pdf" };
static const MimeType PHP{ "application/x-httpd-php", "Hypertext Preprocessor (Personal Home Page)", ".php" };
static const MimeType PPT{ "application/vnd.ms-powerpoint", "Microsoft PowerPoint", ".ppt" };
static const MimeType PPTX{ "application/vnd.openxmlformats-officedocument.presentationml.presentation", "Microsoft PowerPoint (OpenXML)", ".pptx" };
static const MimeType RAR{ "application/vnd.rar", "RAR archive", ".rar" };
static const MimeType RTF{ "application/rtf", "Rich Text Format (RTF)", ".rtf" };
static const MimeType TAR{ "application/x-tar", "Tape Archive (TAR)", ".tar" };
static const MimeType VSD{ "application/vnd.visio", "Microsoft Visio", ".vsd" };
static const MimeType XHTML{ "application/xhtml+xml", "XHTML", ".xhtml" };
static const MimeType XLS{ "application/vnd.ms-excel", "Microsoft Excel", ".xls" };
static const MimeType XLSX{ "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "Microsoft Excel (OpenXML)", ".xlsx" };
static const MimeType ZIP{ "application/zip", "ZIP archive", ".zip" };
/* fall-back */
static const MimeType                  UNKNOWN{ "unknown/unknown", "unknown data format" };

static inline std::array<MimeType, 54> ALL{
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

/**
 * Case-insensitive mapping between MIME-type string and enumumeration value.
 *
 * @param mimeTypeStr the string equivalent mime-type, e.g. "image/png"
 * @return the enumeration equivalent first matching mime-type, e.g. MimeType.PNG or MimeType.UNKNOWN as fall-back
 */
const static MimeType &getType(const std::string_view &mimeTypeStr) {
    if (mimeTypeStr.empty()) {
        return UNKNOWN;
    }
    std::string mimeType(mimeTypeStr);
    std::transform(mimeType.begin(), mimeType.end(), mimeType.begin(), static_cast<int (*)(int)>(std::tolower));
    for (MimeType &mType : ALL) {
        // N.B.mimeType can contain several MIME types, e.g "image/webp,image/apng,image/*"
        if (mimeType.find(mType.typeName()) != std::string::npos) {
            return mType;
        }
    }
    return UNKNOWN;
}

/**
 * Case-insensitive mapping between MIME-type string and enumeration value.
 *
 * @param fileName the string equivalent mime-type, e.g. "image/png"
 * @return the enumeration equivalent mime-type, e.g. MimeType.PNG or MimeType.UNKNOWN as fall-back
 */
const static MimeType &getTypeByFileName(const std::string_view &fileName) {
    if (fileName.empty()) {
        return UNKNOWN;
    }
    constexpr auto ends_with = [](const std::string_view &value, const std::string_view &ending) {
        return ending.size() > value.size() ? false : std::equal(ending.rbegin(), ending.rend(), value.rbegin());
    };
    std::string trimmed(fileName);
    std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), static_cast<int (*)(int)>(std::tolower));

    for (MimeType &mType : ALL) {
        for (auto &ending : mType.fileExtensions()) {
            if (ends_with(trimmed, ending)) {
                return mType;
            }
        }
    }

    return UNKNOWN;
}

} /* namespace opencmw::MIME */

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

template<>
struct fmt::formatter<opencmw::MIME::MimeType> {
            template<typename ParseContext>
            constexpr auto parse(ParseContext &ctx) {
                return ctx.begin(); // not (yet) implemented
            }

            template<typename FormatContext>
            auto format(opencmw::MIME::MimeType const &v, FormatContext &ctx) {
                return fmt::format_to(ctx.out(), "{}", v.typeName());
            }
        };

inline std::ostream &operator<<(std::ostream &os, const opencmw::MIME::MimeType &v) {
    return os << v.typeName();
}

#endif // OPENCMW_CPP_MIME_HPP
