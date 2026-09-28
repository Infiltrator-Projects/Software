// SPDX-License-Identifier: GPL-3.0-or-later
#include "external/cinnamon_spices.hpp"

#include <curl/curl.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kSpiceRoot =
    "https://cinnamon-spices.linuxmint.com";
constexpr std::uint64_t kMaxArchiveBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxExpandedBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxMetadataBytes = 64U * 1024U * 1024U;

struct SpiceDescriptor {
    ExternalUpdateKind kind{};
    const char *type{};
    const char *index_path{};
    const char *plural{};
};

constexpr SpiceDescriptor kSpiceDescriptors[] = {
    {ExternalUpdateKind::cinnamon_applet, "applet", "/json/applets.json", "applets"},
    {ExternalUpdateKind::cinnamon_desklet, "desklet", "/json/desklets.json", "desklets"},
    {ExternalUpdateKind::cinnamon_extension, "extension", "/json/extensions.json", "extensions"},
    {ExternalUpdateKind::cinnamon_theme, "theme", "/json/themes.json", "themes"},
    {ExternalUpdateKind::nemo_action, "action", "/json/actions.json", "actions"},
};

const SpiceDescriptor *descriptor_for(const ExternalUpdateKind kind)
{
    for (const SpiceDescriptor &descriptor : kSpiceDescriptors) {
        if (descriptor.kind == kind) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool curl_ready()
{
    static const CURLcode status = curl_global_init(CURL_GLOBAL_DEFAULT);
    return status == CURLE_OK;
}

struct TextDownloadBuffer {
    std::string *text{};
    std::size_t maximum{0U};
    bool exceeded{false};
};

size_t append_to_string(
    char *data,
    const size_t size,
    const size_t count,
    void *user_data)
{
    auto *buffer =
        static_cast<TextDownloadBuffer *>(user_data);
    if (buffer == nullptr ||
        buffer->text == nullptr ||
        (size != 0U &&
         count >
             std::numeric_limits<std::size_t>::max() /
                 size)) {
        return 0U;
    }

    const size_t bytes = size * count;
    if (buffer->text->size() >
            buffer->maximum ||
        bytes >
            buffer->maximum -
                buffer->text->size()) {
        buffer->exceeded = true;
        return 0U;
    }
    buffer->text->append(data, bytes);
    return bytes;
}

bool fetch_text(
    const std::string &url,
    std::string &text,
    std::string &error)
{
    text.clear();
    error.clear();
    if (!curl_ready()) {
        error = "Unable to initialise the HTTPS client.";
        return false;
    }

    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
        error = "Unable to create the HTTPS request.";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Infiltrator-Software/1");
    TextDownloadBuffer buffer{
        &text,
        kMaxMetadataBytes,
        false};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(
        curl,
        CURLOPT_MAXFILESIZE_LARGE,
        static_cast<curl_off_t>(kMaxMetadataBytes));

    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        error = buffer.exceeded
            ? "Cinnamon Spice metadata exceeded the 64 MiB safety limit."
            : "Unable to fetch Cinnamon Spice metadata: " +
                std::string(curl_easy_strerror(result));
        curl_easy_cleanup(curl);
        return false;
    }

    long response = 0L;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
    curl_easy_cleanup(curl);
    if (response < 200L || response >= 300L) {
        error =
            "Cinnamon Spice metadata server returned HTTP " +
            std::to_string(response) + ".";
        return false;
    }
    return true;
}

bool safe_component(const std::string_view value)
{
    if (value.empty() || value == "." || value == "..") {
        return false;
    }
    return std::none_of(
        value.begin(),
        value.end(),
        [](const unsigned char ch) {
            return ch == '/' || ch == '\\' || ch == '\0' ||
                   ch < 0x20U;
        });
}

std::vector<fs::path> user_install_folders(
    const ExternalUpdateKind kind)
{
    std::vector<fs::path> result;
    const char *data = g_get_user_data_dir();
    const char *home = g_get_home_dir();
    if (data == nullptr || *data == '\0') {
        return result;
    }

    const fs::path data_path(data);
    switch (kind) {
    case ExternalUpdateKind::cinnamon_applet:
        result.push_back(data_path / "cinnamon/applets");
        break;
    case ExternalUpdateKind::cinnamon_desklet:
        result.push_back(data_path / "cinnamon/desklets");
        break;
    case ExternalUpdateKind::cinnamon_extension:
        result.push_back(data_path / "cinnamon/extensions");
        break;
    case ExternalUpdateKind::nemo_action:
        result.push_back(data_path / "nemo/actions");
        break;
    case ExternalUpdateKind::cinnamon_theme:
        if (home != nullptr && *home != '\0') {
            result.emplace_back(fs::path(home) / ".themes");
        }
        result.push_back(data_path / "themes");
        result.push_back(data_path / "cinnamon/themes");
        break;
    default:
        break;
    }
    return result;
}

bool anything_installed(const ExternalUpdateKind kind)
{
    std::error_code ec;
    for (const fs::path &directory : user_install_folders(kind)) {
        if (!fs::is_directory(directory, ec)) {
            ec.clear();
            continue;
        }
        const fs::directory_iterator end;
        fs::directory_iterator iterator(directory, ec);
        if (!ec && iterator != end) {
            return true;
        }
        ec.clear();
    }
    return false;
}

std::optional<std::int64_t> metadata_revision(
    const fs::path &metadata)
{
    if (!fs::is_regular_file(metadata)) {
        return std::nullopt;
    }

    JsonParser *parser = json_parser_new();
    GError *gerror = nullptr;
    const gboolean loaded =
        json_parser_load_from_file(
            parser,
            metadata.c_str(),
            &gerror);
    if (!loaded) {
        g_clear_error(&gerror);
        g_object_unref(parser);
        return std::nullopt;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (root == nullptr ||
        !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return std::nullopt;
    }

    JsonObject *object = json_node_get_object(root);
    std::optional<std::int64_t> result;
    if (json_object_has_member(object, "last-edited")) {
        result = static_cast<std::int64_t>(
            json_object_get_int_member(object, "last-edited"));
    }
    g_object_unref(parser);
    return result;
}

std::optional<std::int64_t> installed_revision(
    const ExternalUpdateKind kind,
    const std::string &identity)
{
    for (const fs::path &directory : user_install_folders(kind)) {
        const auto revision =
            metadata_revision(directory / identity / "metadata.json");
        if (revision.has_value()) {
            return revision;
        }
    }
    return std::nullopt;
}

std::string date_version(const std::int64_t unix_time)
{
    if (unix_time <= 0) {
        return {};
    }
    const std::time_t raw = static_cast<std::time_t>(unix_time);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &raw);
#else
    localtime_r(&raw, &tm);
#endif
    std::ostringstream output;
    output << std::put_time(&tm, "%Y.%m.%d");
    return output.str();
}

std::string json_string(
    JsonObject *object,
    const char *member)
{
    if (object == nullptr ||
        !json_object_has_member(object, member)) {
        return {};
    }
    JsonNode *node = json_object_get_member(object, member);
    if (node == nullptr ||
        json_node_get_value_type(node) != G_TYPE_STRING) {
        return {};
    }
    const char *value = json_node_get_string(node);
    return value == nullptr ? std::string{} : std::string(value);
}

std::int64_t json_integer(
    JsonObject *object,
    const char *member)
{
    if (object == nullptr ||
        !json_object_has_member(object, member)) {
        return 0;
    }
    JsonNode *node = json_object_get_member(object, member);
    if (node == nullptr ||
        !JSON_NODE_HOLDS_VALUE(node)) {
        return 0;
    }
    const GType type = json_node_get_value_type(node);
    if (type != G_TYPE_INT64 &&
        type != G_TYPE_INT &&
        type != G_TYPE_LONG &&
        type != G_TYPE_UINT64 &&
        type != G_TYPE_UINT &&
        type != G_TYPE_ULONG &&
        type != G_TYPE_DOUBLE) {
        return 0;
    }
    return static_cast<std::int64_t>(
        json_node_get_int(node));
}

bool parse_index(
    const SpiceDescriptor &descriptor,
    const std::string &json,
    std::vector<ExternalUpdate> &updates,
    std::string &error)
{
    JsonParser *parser = json_parser_new();
    GError *gerror = nullptr;
    if (!json_parser_load_from_data(
            parser,
            json.c_str(),
            static_cast<gssize>(json.size()),
            &gerror)) {
        error =
            gerror == nullptr || gerror->message == nullptr
                ? "Cinnamon Spice metadata is malformed."
                : gerror->message;
        g_clear_error(&gerror);
        g_object_unref(parser);
        return false;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (root == nullptr ||
        !JSON_NODE_HOLDS_OBJECT(root)) {
        error = "Cinnamon Spice metadata root is not an object.";
        g_object_unref(parser);
        return false;
    }

    JsonObject *index = json_node_get_object(root);
    GList *members = json_object_get_members(index);
    for (GList *item = members; item != nullptr; item = item->next) {
        const char *member =
            static_cast<const char *>(item->data);
        if (member == nullptr || !safe_component(member)) {
            continue;
        }

        JsonNode *node = json_object_get_member(index, member);
        if (node == nullptr || !JSON_NODE_HOLDS_OBJECT(node)) {
            continue;
        }
        JsonObject *record = json_node_get_object(node);
        const std::int64_t remote_revision =
            json_integer(record, "last_edited");
        const auto local_revision =
            installed_revision(descriptor.kind, member);
        if (!local_revision.has_value() ||
            remote_revision <= *local_revision) {
            continue;
        }

        const std::string remote_file =
            json_string(record, "file");
        if (remote_file.empty() ||
            remote_file.front() != '/' ||
            remote_file.find("..") != std::string::npos ||
            remote_file.find("://") != std::string::npos) {
            continue;
        }

        ExternalUpdate update;
        update.kind = descriptor.kind;
        update.backend = "Cinnamon";
        update.id = member;
        update.name = json_string(record, "name");
        if (update.name.empty()) {
            update.name = update.id;
        }
        update.version = date_version(remote_revision);
        update.detail = json_string(record, "description");
        update.ref = remote_file;
        update.remote_revision = remote_revision;
        const std::int64_t size =
            json_integer(record, "file_size");
        if (size > 0) {
            update.download_bytes =
                static_cast<std::uint64_t>(size);
        }
        updates.emplace_back(std::move(update));
    }

    g_list_free(members);
    g_object_unref(parser);
    return true;
}

std::uint16_t read16(
    const std::vector<unsigned char> &bytes,
    const std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::uint32_t read32(
    const std::vector<unsigned char> &bytes,
    const std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

bool span_valid(
    const std::size_t offset,
    const std::size_t length,
    const std::size_t total)
{
    return offset <= total && length <= total - offset;
}

bool safe_archive_path(
    const std::string &name,
    fs::path &relative)
{
    if (name.empty() ||
        name.front() == '/' ||
        name.front() == '\\' ||
        name.find('\\') != std::string::npos ||
        name.find('\0') != std::string::npos) {
        return false;
    }
    relative = fs::path(name).lexically_normal();
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const fs::path &component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

bool inflate_file(
    const unsigned char *input,
    const std::size_t compressed_size,
    const std::uint64_t expected_size,
    std::ofstream &output,
    uLong &crc,
    std::string &error)
{
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        error = "Unable to initialise ZIP decompression.";
        return false;
    }

    stream.next_in =
        const_cast<Bytef *>(
            reinterpret_cast<const Bytef *>(input));
    stream.avail_in =
        static_cast<uInt>(compressed_size);

    std::array<unsigned char, 64U * 1024U> buffer{};
    std::uint64_t written = 0U;
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = buffer.data();
        stream.avail_out =
            static_cast<uInt>(buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced =
            buffer.size() - stream.avail_out;
        if (produced > 0U) {
            if (written + produced > expected_size ||
                written + produced > kMaxExpandedBytes) {
                inflateEnd(&stream);
                error = "Cinnamon Spice archive expanded beyond its declared size.";
                return false;
            }
            output.write(
                reinterpret_cast<const char *>(buffer.data()),
                static_cast<std::streamsize>(produced));
            if (!output) {
                inflateEnd(&stream);
                error = "Unable to write extracted Cinnamon Spice file.";
                return false;
            }
            crc = crc32(
                crc,
                reinterpret_cast<const Bytef *>(buffer.data()),
                static_cast<uInt>(produced));
            written += produced;
        }
    }

    inflateEnd(&stream);
    if (result != Z_STREAM_END || written != expected_size) {
        error = "Cinnamon Spice ZIP entry is truncated or invalid.";
        return false;
    }
    return true;
}

bool extract_zip_safely(
    const fs::path &archive_path,
    const fs::path &destination,
    std::string &error)
{
    std::error_code ec;
    const std::uintmax_t file_size =
        fs::file_size(archive_path, ec);
    if (ec || file_size == 0U ||
        file_size > kMaxArchiveBytes) {
        error = "Cinnamon Spice archive has an invalid size.";
        return false;
    }

    std::ifstream input(archive_path, std::ios::binary);
    std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (!input.eof() || bytes.size() != file_size) {
        error = "Unable to read the downloaded Cinnamon Spice archive.";
        return false;
    }

    constexpr std::uint32_t kEocd = 0x06054b50U;
    constexpr std::uint32_t kCentral = 0x02014b50U;
    constexpr std::uint32_t kLocal = 0x04034b50U;

    std::optional<std::size_t> eocd;
    const std::size_t minimum =
        bytes.size() > 65557U
            ? bytes.size() - 65557U
            : 0U;
    if (bytes.size() >= 22U) {
        for (std::size_t pos = bytes.size() - 22U;; --pos) {
            if (read32(bytes, pos) == kEocd) {
                eocd = pos;
                break;
            }
            if (pos == minimum) {
                break;
            }
        }
    }
    if (!eocd.has_value() ||
        !span_valid(*eocd, 22U, bytes.size())) {
        error = "Cinnamon Spice download is not a valid ZIP archive.";
        return false;
    }

    const std::uint16_t entries = read16(bytes, *eocd + 10U);
    const std::uint32_t central_size = read32(bytes, *eocd + 12U);
    const std::uint32_t central_offset = read32(bytes, *eocd + 16U);
    if (entries == 0xffffU ||
        central_size == 0xffffffffU ||
        central_offset == 0xffffffffU ||
        !span_valid(central_offset, central_size, bytes.size())) {
        error = "Unsupported ZIP64 Cinnamon Spice archive.";
        return false;
    }

    fs::create_directories(destination, ec);
    if (ec) {
        error = "Unable to create the Cinnamon Spice extraction directory.";
        return false;
    }

    std::size_t cursor = central_offset;
    std::uint64_t expanded_total = 0U;
    for (std::uint16_t index = 0U; index < entries; ++index) {
        if (!span_valid(cursor, 46U, bytes.size()) ||
            read32(bytes, cursor) != kCentral) {
            error = "Cinnamon Spice ZIP central directory is invalid.";
            return false;
        }

        const std::uint16_t flags = read16(bytes, cursor + 8U);
        const std::uint16_t method = read16(bytes, cursor + 10U);
        const std::uint32_t expected_crc = read32(bytes, cursor + 16U);
        const std::uint32_t compressed = read32(bytes, cursor + 20U);
        const std::uint32_t uncompressed = read32(bytes, cursor + 24U);
        const std::uint16_t name_length = read16(bytes, cursor + 28U);
        const std::uint16_t extra_length = read16(bytes, cursor + 30U);
        const std::uint16_t comment_length = read16(bytes, cursor + 32U);
        const std::uint32_t external_attributes = read32(bytes, cursor + 38U);
        const std::uint32_t local_offset = read32(bytes, cursor + 42U);
        const std::size_t record_size =
            46U + name_length + extra_length + comment_length;
        if (!span_valid(cursor, record_size, bytes.size())) {
            error = "Cinnamon Spice ZIP entry is truncated.";
            return false;
        }

        const std::string name(
            reinterpret_cast<const char *>(&bytes[cursor + 46U]),
            name_length);
        fs::path relative;
        if (!safe_archive_path(name, relative)) {
            error = "Cinnamon Spice archive contains an unsafe path.";
            return false;
        }

        const std::uint32_t unix_mode =
            external_attributes >> 16U;
        if ((unix_mode & 0170000U) == 0120000U) {
            error = "Cinnamon Spice archive contains a symbolic link.";
            return false;
        }
        if ((flags & 0x0001U) != 0U ||
            (method != 0U && method != 8U)) {
            error = "Cinnamon Spice archive uses an unsupported ZIP feature.";
            return false;
        }

        const fs::path target =
            (destination / relative).lexically_normal();
        const fs::path relative_check =
            target.lexically_relative(destination);
        if (relative_check.empty() ||
            relative_check.native().rfind("..", 0U) == 0U) {
            error = "Cinnamon Spice archive escaped the extraction directory.";
            return false;
        }

        const bool directory =
            !name.empty() && name.back() == '/';
        if (directory) {
            fs::create_directories(target, ec);
            if (ec) {
                error = "Unable to create a Cinnamon Spice directory.";
                return false;
            }
            cursor += record_size;
            continue;
        }

        if (expanded_total + uncompressed > kMaxExpandedBytes) {
            error = "Cinnamon Spice archive is too large after extraction.";
            return false;
        }
        expanded_total += uncompressed;

        if (!span_valid(local_offset, 30U, bytes.size()) ||
            read32(bytes, local_offset) != kLocal) {
            error = "Cinnamon Spice ZIP local header is invalid.";
            return false;
        }
        const std::uint16_t local_name_length =
            read16(bytes, local_offset + 26U);
        const std::uint16_t local_extra_length =
            read16(bytes, local_offset + 28U);
        const std::size_t data_offset =
            static_cast<std::size_t>(local_offset) +
            30U + local_name_length + local_extra_length;
        if (!span_valid(data_offset, compressed, bytes.size())) {
            error = "Cinnamon Spice ZIP payload is truncated.";
            return false;
        }

        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            error = "Unable to create a Cinnamon Spice parent directory.";
            return false;
        }
        std::ofstream output(
            target,
            std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Unable to create an extracted Cinnamon Spice file.";
            return false;
        }

        uLong crc = crc32(0L, Z_NULL, 0);
        if (method == 0U) {
            if (compressed != uncompressed) {
                error = "Stored Cinnamon Spice ZIP entry has inconsistent sizes.";
                return false;
            }
            output.write(
                reinterpret_cast<const char *>(&bytes[data_offset]),
                static_cast<std::streamsize>(compressed));
            crc = crc32(
                crc,
                reinterpret_cast<const Bytef *>(&bytes[data_offset]),
                compressed);
        } else if (!inflate_file(
                       &bytes[data_offset],
                       compressed,
                       uncompressed,
                       output,
                       crc,
                       error)) {
            return false;
        }
        output.close();
        if (!output || crc != expected_crc) {
            error = "Cinnamon Spice ZIP entry failed integrity verification.";
            return false;
        }

        const std::uint32_t permissions =
            unix_mode & 0777U;
        if (permissions != 0U) {
            /*
             * Spice archives are remote content. Preserve whether a file is
             * executable, but never allow an archive to publish group/world
             * writable payloads or strip the owner's read/write access.
             */
            const std::uint32_t safe_permissions =
                (permissions & 0111U) != 0U
                    ? 0755U
                    : 0644U;
            fs::permissions(
                target,
                static_cast<fs::perms>(safe_permissions),
                fs::perm_options::replace,
                ec);
            if (ec) {
                error =
                    "Unable to apply safe Cinnamon Spice file permissions.";
                return false;
            }
        }
        cursor += record_size;
    }
    return true;
}

struct DownloadContext {
    FILE *file{};
    ExternalProgressCallback progress;
    std::string label;
    std::size_t index{0U};
    std::size_t count{0U};
    std::uint64_t written{0U};
    bool exceeded{false};
};

size_t write_download(
    char *data,
    const size_t size,
    const size_t count,
    void *user_data)
{
    auto *context =
        static_cast<DownloadContext *>(user_data);
    if (context == nullptr ||
        context->file == nullptr ||
        (size != 0U &&
         count >
             std::numeric_limits<std::size_t>::max() /
                 size)) {
        return 0U;
    }
    const std::size_t bytes = size * count;
    if (context->written >
            kMaxArchiveBytes ||
        static_cast<std::uint64_t>(bytes) >
            kMaxArchiveBytes -
                context->written) {
        context->exceeded = true;
        return 0U;
    }
    const std::size_t written =
        std::fwrite(data, 1U, bytes, context->file);
    context->written +=
        static_cast<std::uint64_t>(written);
    return written;
}

int report_download(
    void *user_data,
    curl_off_t total,
    curl_off_t now,
    curl_off_t,
    curl_off_t)
{
    auto *context =
        static_cast<DownloadContext *>(user_data);
    if (!context->progress) {
        return 0;
    }

    std::ostringstream status;
    status << "Cinnamon " << context->index
           << "/" << context->count
           << " • " << context->label
           << " • downloading";
    if (total > 0) {
        const auto percent =
            static_cast<unsigned long long>(
                (now * 100) / total);
        status << " " << percent << "%";
    } else if (now > 0) {
        status << " "
               << static_cast<unsigned long long>(now)
               << " bytes";
    }
    context->progress(status.str());
    return 0;
}

bool reviewed_spice_is_current(
    const ExternalUpdate &reviewed,
    std::string &error)
{
    const SpiceDescriptor *descriptor =
        descriptor_for(reviewed.kind);
    if (descriptor == nullptr) {
        error = "Unknown Cinnamon Spice type.";
        return false;
    }

    std::string json;
    if (!fetch_text(
            std::string(kSpiceRoot) + descriptor->index_path,
            json,
            error)) {
        return false;
    }

    std::vector<ExternalUpdate> current;
    if (!parse_index(*descriptor, json, current, error)) {
        return false;
    }

    const auto found = std::find_if(
        current.begin(), current.end(),
        [&](const ExternalUpdate &candidate) {
            return candidate.id == reviewed.id;
        });
    if (found == current.end()) {
        error =
            "Cinnamon Spice metadata changed after review for " +
            reviewed.id + ".";
        return false;
    }

    if (found->remote_revision != reviewed.remote_revision ||
        found->ref != reviewed.ref ||
        found->download_bytes != reviewed.download_bytes) {
        error =
            "Cinnamon Spice metadata changed after review for " +
            reviewed.id + "; review the refreshed update before installing it.";
        return false;
    }
    return true;
}

bool download_archive(
    const ExternalUpdate &update,
    const std::size_t index,
    const std::size_t count,
    const fs::path &target,
    std::string &error,
    const ExternalProgressCallback &progress)
{
    if (update.ref.empty() ||
        update.ref.front() != '/' ||
        update.ref.find("..") != std::string::npos ||
        update.ref.find("://") != std::string::npos) {
        error = "Cinnamon Spice update contains an invalid download path.";
        return false;
    }
    if (!curl_ready()) {
        error = "Unable to initialise the HTTPS client.";
        return false;
    }

    FILE *file = std::fopen(target.c_str(), "wb");
    if (file == nullptr) {
        error = "Unable to create the temporary Cinnamon Spice download.";
        return false;
    }

    DownloadContext context{
        file,
        progress,
        update.name.empty() ? update.id : update.name,
        index,
        count,
        0U,
        false
    };
    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
        std::fclose(file);
        error = "Unable to create the Cinnamon Spice HTTPS request.";
        return false;
    }

    const std::string url =
        std::string(kSpiceRoot) + update.ref;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Infiltrator-Software/1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_download);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, report_download);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);
    curl_easy_setopt(
        curl,
        CURLOPT_MAXFILESIZE_LARGE,
        static_cast<curl_off_t>(kMaxArchiveBytes));

    const CURLcode result = curl_easy_perform(curl);
    long response = 0L;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
    curl_easy_cleanup(curl);
    const bool closed = std::fclose(file) == 0;

    if (result != CURLE_OK || !closed ||
        response < 200L || response >= 300L) {
        error =
            "Unable to download Cinnamon Spice " +
            update.id + ": " +
            (context.exceeded
                 ? "archive exceeded the 256 MiB safety limit"
                 : result == CURLE_OK
                     ? "HTTP " + std::to_string(response)
                     : std::string(curl_easy_strerror(result)));
        return false;
    }

    std::error_code ec;
    const std::uintmax_t bytes = fs::file_size(target, ec);
    if (ec || bytes == 0U || bytes > kMaxArchiveBytes) {
        error = "Downloaded Cinnamon Spice archive has an invalid size.";
        return false;
    }
    if (update.download_bytes > 0U &&
        bytes != update.download_bytes) {
        error =
            "Downloaded Cinnamon Spice archive size no longer matches the "
            "reviewed metadata.";
        return false;
    }
    return true;
}

bool run_command(
    const std::vector<std::string> &arguments,
    std::string &error)
{
    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    GError *gerror = nullptr;
    GSubprocess *process =
        g_subprocess_newv(
            argv.data(),
            static_cast<GSubprocessFlags>(
                G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                G_SUBPROCESS_FLAGS_STDERR_PIPE),
            &gerror);
    if (process == nullptr) {
        error =
            gerror == nullptr || gerror->message == nullptr
                ? "Unable to start Cinnamon Spice helper."
                : gerror->message;
        g_clear_error(&gerror);
        return false;
    }

    gchar *stderr_text = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8(
            process,
            nullptr,
            nullptr,
            nullptr,
            &stderr_text,
            &gerror);
    const bool success =
        communicated &&
        g_subprocess_get_successful(process);
    if (!success) {
        error =
            stderr_text != nullptr && *stderr_text != '\0'
                ? stderr_text
                : gerror != nullptr && gerror->message != nullptr
                    ? gerror->message
                    : "Cinnamon Spice helper failed.";
    }
    g_free(stderr_text);
    g_clear_error(&gerror);
    g_object_unref(process);
    return success;
}

bool write_revision(
    const fs::path &metadata,
    const std::int64_t revision,
    std::string &error)
{
    JsonNode *owned_root = nullptr;
    JsonParser *parser = nullptr;
    JsonNode *root = nullptr;

    if (fs::is_regular_file(metadata)) {
        parser = json_parser_new();
        GError *gerror = nullptr;
        if (!json_parser_load_from_file(
                parser,
                metadata.c_str(),
                &gerror)) {
            error =
                gerror == nullptr || gerror->message == nullptr
                    ? "Unable to read Cinnamon Spice metadata."
                    : gerror->message;
            g_clear_error(&gerror);
            g_object_unref(parser);
            return false;
        }
        root = json_parser_get_root(parser);
        if (root == nullptr || !JSON_NODE_HOLDS_OBJECT(root)) {
            error = "Cinnamon Spice metadata is not an object.";
            g_object_unref(parser);
            return false;
        }
    } else {
        owned_root = json_node_new(JSON_NODE_OBJECT);
        json_node_take_object(
            owned_root,
            json_object_new());
        root = owned_root;
    }

    JsonObject *object = json_node_get_object(root);
    json_object_set_int_member(
        object,
        "last-edited",
        revision);

    JsonGenerator *generator =
        json_generator_new();
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);
    gsize length = 0U;
    gchar *data =
        json_generator_to_data(generator, &length);

    std::ofstream output(
        metadata,
        std::ios::binary | std::ios::trunc);
    if (data == nullptr || !output) {
        error = "Unable to write Cinnamon Spice metadata.";
        g_free(data);
        g_object_unref(generator);
        if (parser != nullptr) g_object_unref(parser);
        if (owned_root != nullptr) json_node_free(owned_root);
        return false;
    }
    output.write(
        data,
        static_cast<std::streamsize>(length));
    output << '\n';
    const bool success = static_cast<bool>(output);

    g_free(data);
    g_object_unref(generator);
    if (parser != nullptr) g_object_unref(parser);
    if (owned_root != nullptr) json_node_free(owned_root);

    if (!success) {
        error = "Unable to persist Cinnamon Spice metadata.";
    }
    return success;
}

struct TranslationArtifact {
    fs::path staged;
    fs::path destination;
};

void remove_staged_translations(
    const std::vector<TranslationArtifact> &artifacts) noexcept
{
    std::error_code ignored;
    for (const TranslationArtifact &artifact : artifacts) {
        fs::remove(artifact.staged, ignored);
        ignored.clear();
    }
}

bool compile_translations(
    const fs::path &source,
    const std::string &identity,
    std::vector<TranslationArtifact> &artifacts,
    std::string &error)
{
    artifacts.clear();
    const fs::path po = source / "po";
    if (!fs::is_directory(po)) {
        return true;
    }

    gchar *msgfmt = g_find_program_in_path("msgfmt");
    if (msgfmt == nullptr) {
        error =
            "This Cinnamon Spice contains translations, but msgfmt is unavailable.";
        return false;
    }
    g_free(msgfmt);

    const char *data = g_get_user_data_dir();
    if (data == nullptr || *data == '\0') {
        error = "The user data directory is unavailable.";
        return false;
    }

    std::error_code ec;
    for (const fs::directory_entry &entry :
         fs::directory_iterator(po, ec)) {
        if (ec) {
            remove_staged_translations(artifacts);
            artifacts.clear();
            error = "Unable to enumerate Cinnamon Spice translations.";
            return false;
        }
        if (!entry.is_regular_file() ||
            entry.path().extension() != ".po") {
            continue;
        }

        const std::string language =
            entry.path().stem().string();
        if (!safe_component(language)) {
            remove_staged_translations(artifacts);
            artifacts.clear();
            error = "Cinnamon Spice contains an unsafe translation name.";
            return false;
        }

        const fs::path destination =
            fs::path(data) / "locale" /
            language / "LC_MESSAGES" /
            (identity + ".mo");
        fs::create_directories(
            destination.parent_path(),
            ec);
        if (ec) {
            remove_staged_translations(artifacts);
            artifacts.clear();
            error = "Unable to create the Cinnamon Spice locale directory.";
            return false;
        }

        std::string pattern =
            (destination.parent_path() /
             ("." + identity + "-" + language +
              "-XXXXXX")).string();
        std::vector<char> writable(
            pattern.begin(),
            pattern.end());
        writable.push_back('\0');
        const int descriptor = mkstemp(writable.data());
        if (descriptor < 0) {
            remove_staged_translations(artifacts);
            artifacts.clear();
            error =
                "Unable to stage a Cinnamon Spice translation.";
            return false;
        }
        if (close(descriptor) != 0) {
            const fs::path failed(writable.data());
            fs::remove(failed, ec);
            remove_staged_translations(artifacts);
            artifacts.clear();
            error =
                "Unable to close a staged Cinnamon Spice translation.";
            return false;
        }

        TranslationArtifact artifact{
            fs::path(writable.data()),
            destination};
        if (!run_command(
                {"msgfmt", "-c",
                 entry.path().string(),
                 "-o", artifact.staged.string()},
                error)) {
            fs::remove(artifact.staged, ec);
            remove_staged_translations(artifacts);
            artifacts.clear();
            return false;
        }
        artifacts.emplace_back(
            std::move(artifact));
    }
    return true;
}

struct PublishedPath {
    fs::path staged;
    fs::path destination;
    fs::path backup;
    bool had_existing{false};
    bool new_present{false};
};

bool write_all_fd(
    const int fd,
    const std::string_view content)
{
    std::size_t offset = 0U;
    while (offset < content.size()) {
        const ssize_t written =
            write(
                fd,
                content.data() + offset,
                content.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool sync_directory_path(
    const fs::path &directory)
{
    const int fd =
        open(
            directory.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool synced = fsync(fd) == 0;
    const bool closed = close(fd) == 0;
    return synced && closed;
}

bool write_spice_journal(
    const fs::path &journal,
    const std::vector<PublishedPath> &intents,
    std::string &error)
{
    std::ostringstream content;
    content << "INFLTR-SPICE-JOURNAL 1\n";
    for (const PublishedPath &intent : intents) {
        content
            << std::quoted(intent.destination.string()) << ' '
            << std::quoted(intent.backup.string()) << ' '
            << std::quoted(intent.staged.string()) << ' '
            << (intent.had_existing ? 1 : 0) << '\n';
    }

    std::string pattern =
        (journal.parent_path() /
         ".infiltrator-spice-journal-XXXXXX").string();
    std::vector<char> writable(
        pattern.begin(),
        pattern.end());
    writable.push_back('\0');
    const int fd = mkstemp(writable.data());
    if (fd < 0) {
        error = "Unable to create Cinnamon Spice recovery journal.";
        return false;
    }

    const fs::path temporary(writable.data());
    const std::string bytes = content.str();
    bool ok =
        fchmod(fd, 0600) == 0 &&
        write_all_fd(fd, bytes) &&
        fsync(fd) == 0;
    if (close(fd) != 0) {
        ok = false;
    }
    if (!ok ||
        rename(temporary.c_str(), journal.c_str()) != 0 ||
        !sync_directory_path(journal.parent_path())) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        error =
            "Unable to durably publish Cinnamon Spice recovery journal.";
        return false;
    }
    return true;
}

bool clear_spice_journal(
    const fs::path &journal,
    std::string &error)
{
    std::error_code ec;
    if (!fs::exists(journal, ec)) {
        if (ec) {
            error =
                "Unable to inspect Cinnamon Spice recovery journal: " +
                ec.message();
            return false;
        }
        return true;
    }
    if (!fs::remove(journal, ec) || ec) {
        error =
            "Unable to remove completed Cinnamon Spice recovery journal: " +
            ec.message();
        return false;
    }
    if (!sync_directory_path(journal.parent_path())) {
        error =
            "Cinnamon Spice recovery journal was removed, but its directory could not be synchronized.";
        return false;
    }
    return true;
}

bool load_spice_journal(
    const fs::path &journal,
    std::vector<PublishedPath> &intents,
    std::string &error)
{
    intents.clear();
    std::error_code ec;
    if (!fs::exists(journal, ec)) {
        if (ec) {
            error =
                "Unable to inspect Cinnamon Spice recovery journal: " +
                ec.message();
            return false;
        }
        return true;
    }

    std::ifstream input(journal, std::ios::binary);
    if (!input) {
        error = "Unable to read Cinnamon Spice recovery journal.";
        return false;
    }
    std::string header;
    std::getline(input, header);
    if (header != "INFLTR-SPICE-JOURNAL 1") {
        error =
            "Cinnamon Spice recovery journal has an unsupported format.";
        return false;
    }

    while (input) {
        std::string destination;
        std::string backup;
        std::string staged;
        int had_existing = 0;
        if (!(input >> std::quoted(destination))) {
            break;
        }
        if (!(input >> std::quoted(backup) >>
              std::quoted(staged) >> had_existing) ||
            (had_existing != 0 && had_existing != 1)) {
            error = "Cinnamon Spice recovery journal is malformed.";
            intents.clear();
            return false;
        }
        PublishedPath intent;
        intent.destination = destination;
        intent.backup = backup;
        intent.staged = staged;
        intent.had_existing = had_existing != 0;
        intents.emplace_back(std::move(intent));
    }
    return true;
}

fs::path backup_path(
    const fs::path &destination,
    const std::size_t ordinal)
{
    return destination.string() +
        ".infiltrator-old-" +
        std::to_string(
            static_cast<unsigned long long>(getpid())) +
        "-" + std::to_string(ordinal);
}

bool exchange_paths(
    const fs::path &left,
    const fs::path &right,
    std::string &error)
{
#if defined(SYS_renameat2)
#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1U << 1U)
#endif
    if (syscall(
            SYS_renameat2,
            AT_FDCWD,
            left.c_str(),
            AT_FDCWD,
            right.c_str(),
            RENAME_EXCHANGE) == 0) {
        return true;
    }
    error =
        "Unable to atomically exchange Cinnamon Spice paths: " +
        std::string(std::strerror(errno));
    return false;
#else
    (void)left;
    (void)right;
    error =
        "This Linux runtime does not provide renameat2 path exchange.";
    return false;
#endif
}

bool publish_path(
    const fs::path *staged,
    const fs::path &destination,
    const fs::path &journal,
    std::vector<PublishedPath> &published,
    std::string &error)
{
    std::error_code ec;
    fs::create_directories(
        destination.parent_path(), ec);
    if (ec) {
        error =
            "Unable to create Cinnamon Spice destination directory: " +
            ec.message();
        return false;
    }

    PublishedPath change;
    if (staged != nullptr) {
        change.staged = *staged;
    }
    change.destination = destination;
    change.backup =
        backup_path(
            destination,
            published.size());
    if (fs::exists(change.backup, ec) || ec) {
        error =
            "A stale Cinnamon Spice transaction backup blocks installation: " +
            change.backup.string();
        return false;
    }

    change.had_existing =
        fs::exists(destination, ec);
    if (ec) {
        error =
            "Unable to inspect the existing Cinnamon Spice installation.";
        return false;
    }

    published.emplace_back(change);
    if (!write_spice_journal(
            journal,
            published,
            error)) {
        published.pop_back();
        return false;
    }
    PublishedPath &journalled = published.back();

    if (staged != nullptr) {
        if (journalled.had_existing) {
            /*
             * Linux renameat2(RENAME_EXCHANGE) keeps one complete version at
             * the live destination throughout the replacement. A crash can
             * therefore leave either the old or new complete tree live, but
             * never the empty rename window created by move-old-then-move-new.
             */
            std::string exchange_error;
            if (!exchange_paths(
                    *staged,
                    destination,
                    exchange_error)) {
                error = exchange_error;
                return false;
            }

            fs::rename(
                *staged,
                journalled.backup,
                ec);
            if (ec) {
                const std::string backup_error =
                    ec.message();
                std::string restore_error;
                if (!exchange_paths(
                        *staged,
                        destination,
                        restore_error)) {
                    error =
                        "Cinnamon Spice replacement was exchanged but its "
                        "rollback copy could not be retained (" +
                        backup_error +
                        "); restoring the previous live copy also failed: " +
                        restore_error;
                } else {
                    error =
                        "Cinnamon Spice replacement was rolled back because "
                        "its previous copy could not be retained: " +
                        backup_error;
                }
                return false;
            }
        } else {
            fs::rename(
                *staged,
                destination,
                ec);
            if (ec) {
                error =
                    "Unable to atomically publish the Cinnamon Spice update: " +
                    ec.message();
                return false;
            }
        }
        journalled.new_present = true;
    } else if (journalled.had_existing) {
        fs::rename(
            destination,
            journalled.backup,
            ec);
        if (ec) {
            error =
                "Unable to stage the existing Cinnamon Spice for rollback: " +
                ec.message();
            return false;
        }
    }

    return true;
}

bool recover_spice_journal(
    const fs::path &journal,
    std::string &error)
{
    std::vector<PublishedPath> intents;
    if (!load_spice_journal(
            journal,
            intents,
            error)) {
        return false;
    }
    if (intents.empty()) {
        return true;
    }

    std::error_code ec;
    for (auto iterator = intents.rbegin();
         iterator != intents.rend();
         ++iterator) {
        PublishedPath &intent = *iterator;
        const bool backup_exists =
            fs::exists(intent.backup, ec);
        if (ec) {
            error =
                "Unable to inspect Cinnamon Spice rollback copy: " +
                ec.message();
            return false;
        }

        const bool destination_exists =
            fs::exists(intent.destination, ec);
        if (ec) {
            error =
                "Unable to inspect Cinnamon Spice live path during recovery: " +
                ec.message();
            return false;
        }

        const bool staged_exists =
            !intent.staged.empty() &&
            fs::exists(intent.staged, ec);
        if (ec) {
            error =
                "Unable to inspect Cinnamon Spice staged path during recovery: " +
                ec.message();
            return false;
        }

        if (intent.had_existing) {
            if (backup_exists) {
                if (destination_exists) {
                    fs::remove_all(intent.destination, ec);
                    if (ec) {
                        error =
                            "Unable to remove partially published Cinnamon Spice path during recovery: " +
                            ec.message();
                        return false;
                    }
                }
                fs::rename(
                    intent.backup,
                    intent.destination,
                    ec);
                if (ec) {
                    error =
                        "Unable to restore Cinnamon Spice rollback copy: " +
                        ec.message();
                    return false;
                }
            } else if (staged_exists && destination_exists) {
                /*
                 * A crash may occur after RENAME_EXCHANGE but before the old
                 * live tree is renamed to its backup. In that narrow window
                 * the staged path contains the previous live copy.
                 */
                std::string exchange_error;
                if (!exchange_paths(
                        intent.staged,
                        intent.destination,
                        exchange_error)) {
                    error =
                        "Unable to recover interrupted Cinnamon Spice exchange: " +
                        exchange_error;
                    return false;
                }
                fs::remove_all(intent.staged, ec);
                if (ec) {
                    error =
                        "Unable to remove recovered Cinnamon Spice staging path: " +
                        ec.message();
                    return false;
                }
            } else if (!destination_exists) {
                error =
                    "Cinnamon Spice recovery cannot locate either the live path or its rollback copy.";
                return false;
            }
        } else {
            if (destination_exists) {
                fs::remove_all(intent.destination, ec);
                if (ec) {
                    error =
                        "Unable to remove partially published Cinnamon Spice path during recovery: " +
                        ec.message();
                    return false;
                }
            }
            if (staged_exists) {
                fs::remove_all(intent.staged, ec);
                if (ec) {
                    error =
                        "Unable to remove stale Cinnamon Spice staged path during recovery: " +
                        ec.message();
                    return false;
                }
            }
        }
    }

    return clear_spice_journal(journal, error);
}

void rollback_published_paths(
    std::vector<PublishedPath> &published,
    std::string &error)
{
    std::string rollback_error;
    std::error_code ec;
    for (auto iterator = published.rbegin();
         iterator != published.rend();
         ++iterator) {
        if (iterator->new_present) {
            fs::remove_all(
                iterator->destination,
                ec);
            if (ec && rollback_error.empty()) {
                rollback_error =
                    "Unable to remove partially published path " +
                    iterator->destination.string() + ": " +
                    ec.message() + ".";
            }
            ec.clear();
        }
        if (iterator->had_existing) {
            fs::rename(
                iterator->backup,
                iterator->destination,
                ec);
            if (ec && rollback_error.empty()) {
                rollback_error =
                    "Unable to restore previous path " +
                    iterator->destination.string() + ": " +
                    ec.message() + ".";
            }
            ec.clear();
        }
    }

    if (!rollback_error.empty()) {
        if (!error.empty()) {
            error += " ";
        }
        error +=
            "Cinnamon Spice rollback was incomplete: " +
            rollback_error;
    }
}

void discard_backups(
    const std::vector<PublishedPath> &published) noexcept
{
    std::error_code ignored;
    for (const PublishedPath &change : published) {
        if (change.had_existing) {
            fs::remove_all(
                change.backup,
                ignored);
            ignored.clear();
        }
    }
}

bool install_extracted(
    const ExternalUpdate &update,
    const fs::path &extracted,
    std::string &error)
{
    const auto folders =
        user_install_folders(update.kind);
    if (folders.empty()) {
        error = "No writable Cinnamon Spice installation directory exists.";
        return false;
    }

    const fs::path source =
        extracted / update.id;
    if (!fs::is_directory(source)) {
        error =
            "Downloaded Cinnamon Spice does not contain its expected " +
            update.id + " directory.";
        return false;
    }

    std::vector<TranslationArtifact> translations;
    if (!compile_translations(
            source,
            update.id,
            translations,
            error)) {
        return false;
    }

    const fs::path destination_root =
        folders.front();
    std::error_code ec;
    fs::create_directories(
        destination_root, ec);
    if (ec) {
        remove_staged_translations(translations);
        error =
            "Unable to create the Cinnamon Spice installation directory.";
        return false;
    }

    const fs::path journal =
        destination_root /
        (".infiltrator-spice-" + update.id + ".journal");
    if (!recover_spice_journal(
            journal,
            error)) {
        remove_staged_translations(translations);
        return false;
    }

    const fs::path stage_root =
        destination_root /
        (".infiltrator-stage-" +
         update.id + "-" +
         std::to_string(
             static_cast<unsigned long long>(getpid())));
    if (fs::exists(stage_root, ec) || ec) {
        remove_staged_translations(translations);
        error =
            "A stale Cinnamon Spice staging directory blocks installation.";
        return false;
    }
    fs::create_directories(stage_root, ec);
    if (ec) {
        remove_staged_translations(translations);
        error =
            "Unable to create the Cinnamon Spice staging directory.";
        return false;
    }

    if (update.kind == ExternalUpdateKind::nemo_action) {
        for (const fs::directory_entry &entry :
             fs::directory_iterator(extracted, ec)) {
            if (ec) {
                fs::remove_all(stage_root, ec);
                remove_staged_translations(translations);
                error =
                    "Unable to enumerate the Nemo action payload.";
                return false;
            }
            fs::copy(
                entry.path(),
                stage_root / entry.path().filename(),
                fs::copy_options::recursive,
                ec);
            if (ec) {
                fs::remove_all(stage_root, ec);
                remove_staged_translations(translations);
                error =
                    "Unable to stage the Nemo action payload: " +
                    ec.message();
                return false;
            }
        }
    } else {
        fs::copy(
            source,
            stage_root / update.id,
            fs::copy_options::recursive,
            ec);
        if (ec) {
            fs::remove_all(stage_root, ec);
            remove_staged_translations(translations);
            error =
                "Unable to stage Cinnamon Spice " +
                update.id + ": " + ec.message();
            return false;
        }
    }

    const fs::path staged_metadata =
        stage_root / update.id / "metadata.json";
    if (!write_revision(
            staged_metadata,
            update.remote_revision,
            error)) {
        fs::remove_all(stage_root, ec);
        remove_staged_translations(translations);
        return false;
    }

    std::vector<fs::path> staged_payloads;
    for (const fs::directory_entry &entry :
         fs::directory_iterator(stage_root, ec)) {
        if (ec) {
            fs::remove_all(stage_root, ec);
            remove_staged_translations(translations);
            error =
                "Unable to enumerate the staged Cinnamon Spice payload.";
            return false;
        }
        staged_payloads.emplace_back(entry.path());
    }

    std::vector<PublishedPath> published;
    for (const fs::path &staged : staged_payloads) {
        if (!publish_path(
                &staged,
                destination_root /
                    staged.filename(),
                journal,
                published,
                error)) {
            rollback_published_paths(published, error);
            std::string journal_error;
            if (!clear_spice_journal(journal, journal_error) &&
                !journal_error.empty()) {
                if (!error.empty()) error += " ";
                error += journal_error;
            }
            fs::remove_all(stage_root, ec);
            remove_staged_translations(translations);
            return false;
        }
    }

    /*
     * Remove duplicate copies from the alternate user Spice locations only
     * after the new primary copy is live. Rename them into transaction
     * backups so every deletion can be restored if a later step fails.
     */
    for (std::size_t index = 1U;
         index < folders.size();
         ++index) {
        const fs::path duplicate =
            folders[index] / update.id;
        if (fs::exists(duplicate, ec)) {
            if (ec ||
                !publish_path(
                    nullptr,
                    duplicate,
                    journal,
                    published,
                    error)) {
                rollback_published_paths(published, error);
                std::string journal_error;
                if (!clear_spice_journal(journal, journal_error) &&
                    !journal_error.empty()) {
                    if (!error.empty()) error += " ";
                    error += journal_error;
                }
                fs::remove_all(stage_root, ec);
                remove_staged_translations(translations);
                return false;
            }
        }
        ec.clear();

        if (update.kind ==
            ExternalUpdateKind::nemo_action) {
            const fs::path action =
                folders[index] /
                (update.id + ".nemo_action");
            if (fs::exists(action, ec)) {
                if (ec ||
                    !publish_path(
                        nullptr,
                        action,
                        journal,
                        published,
                        error)) {
                    rollback_published_paths(published, error);
                    std::string journal_error;
                    if (!clear_spice_journal(journal, journal_error) &&
                        !journal_error.empty()) {
                        if (!error.empty()) error += " ";
                        error += journal_error;
                    }
                    fs::remove_all(stage_root, ec);
                    remove_staged_translations(translations);
                    return false;
                }
            }
            ec.clear();
        }
    }

    for (TranslationArtifact &artifact :
         translations) {
        if (!publish_path(
                &artifact.staged,
                artifact.destination,
                journal,
                published,
                error)) {
            rollback_published_paths(published, error);
            std::string journal_error;
            if (!clear_spice_journal(journal, journal_error) &&
                !journal_error.empty()) {
                if (!error.empty()) error += " ";
                error += journal_error;
            }
            fs::remove_all(stage_root, ec);
            remove_staged_translations(translations);
            return false;
        }
    }

    fs::remove_all(stage_root, ec);
    discard_backups(published);
    if (!clear_spice_journal(journal, error)) {
        return false;
    }
    return true;
}

bool schema_available(const char *schema)
{
    GSettingsSchemaSource *source =
        g_settings_schema_source_get_default();
    if (source == nullptr) {
        return false;
    }
    GSettingsSchema *found =
        g_settings_schema_source_lookup(
            source,
            schema,
            TRUE);
    if (found == nullptr) {
        return false;
    }
    g_settings_schema_unref(found);
    return true;
}

bool string_list_contains_identity(
    gchar **values,
    const std::string &identity,
    const bool colon_format)
{
    if (values == nullptr) {
        return false;
    }
    for (gchar **item = values; *item != nullptr; ++item) {
        std::string value(*item);
        value.erase(
            std::remove(value.begin(), value.end(), '!'),
            value.end());
        if (!colon_format) {
            if (value == identity) return true;
            continue;
        }
        std::size_t start = 0U;
        while (start <= value.size()) {
            const std::size_t end = value.find(':', start);
            const std::string_view field(
                value.data() + start,
                (end == std::string::npos ? value.size() : end) - start);
            if (field == identity) return true;
            if (end == std::string::npos) break;
            start = end + 1U;
        }
    }
    return false;
}

bool spice_is_enabled(const ExternalUpdate &update)
{
    const char *schema = nullptr;
    const char *key = nullptr;
    bool colon_format = false;
    switch (update.kind) {
    case ExternalUpdateKind::cinnamon_applet:
        schema = "org.cinnamon";
        key = "enabled-applets";
        colon_format = true;
        break;
    case ExternalUpdateKind::cinnamon_desklet:
        schema = "org.cinnamon";
        key = "enabled-desklets";
        colon_format = true;
        break;
    case ExternalUpdateKind::cinnamon_extension:
        schema = "org.cinnamon";
        key = "enabled-extensions";
        break;
    case ExternalUpdateKind::cinnamon_theme:
        schema = "org.cinnamon.theme";
        key = "name";
        break;
    case ExternalUpdateKind::nemo_action:
        schema = "org.nemo.plugins";
        key = "disabled-actions";
        break;
    default:
        return false;
    }

    if (!schema_available(schema)) {
        return true;
    }
    GSettings *settings = g_settings_new(schema);
    if (settings == nullptr) {
        return true;
    }

    bool enabled = false;
    if (update.kind == ExternalUpdateKind::cinnamon_theme) {
        gchar *value = g_settings_get_string(settings, key);
        enabled =
            value != nullptr && update.id == value;
        g_free(value);
    } else {
        gchar **values = g_settings_get_strv(settings, key);
        if (update.kind == ExternalUpdateKind::nemo_action) {
            enabled =
                !string_list_contains_identity(
                    values,
                    update.id + ".nemo_action",
                    false);
        } else {
            enabled =
                string_list_contains_identity(
                    values,
                    update.id,
                    colon_format);
        }
        g_strfreev(values);
    }
    g_object_unref(settings);
    return enabled;
}

bool restart_cinnamon_if_needed(
    const bool needed,
    std::string &error)
{
    if (!needed) {
        return true;
    }
    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    if (desktop == nullptr ||
        (std::string_view(desktop) != "Cinnamon" &&
         std::string_view(desktop) != "X-Cinnamon")) {
        return true;
    }
    gchar *command =
        g_find_program_in_path("cinnamon-dbus-command");
    if (command == nullptr) {
        return true;
    }
    g_free(command);

    std::string restart_error;
    if (!run_command(
            {"cinnamon-dbus-command", "RestartCinnamon", "0"},
            restart_error)) {
        error =
            "Cinnamon updates were installed, but the desktop restart failed: " +
            restart_error;
        return false;
    }
    return true;
}

} // namespace

bool discover_native_cinnamon_updates(
    std::vector<ExternalUpdate> &updates,
    std::string &error)
{
    updates.clear();
    error.clear();

    for (const SpiceDescriptor &descriptor :
         kSpiceDescriptors) {
        if (!anything_installed(descriptor.kind)) {
            continue;
        }

        std::string json;
        if (!fetch_text(
                std::string(kSpiceRoot) +
                    descriptor.index_path,
                json,
                error)) {
            updates.clear();
            return false;
        }
        if (!parse_index(
                descriptor,
                json,
                updates,
                error)) {
            updates.clear();
            return false;
        }
    }

    std::sort(
        updates.begin(),
        updates.end(),
        [](const ExternalUpdate &left,
           const ExternalUpdate &right) {
            if (left.kind != right.kind) {
                return static_cast<int>(left.kind) <
                       static_cast<int>(right.kind);
            }
            return left.name < right.name;
        });
    return true;
}

bool apply_native_cinnamon_updates_selected(
    const std::vector<ExternalUpdate> &selected,
    std::string &error,
    ExternalProgressCallback progress,
    std::vector<ExternalUpdate> *completed)
{
    error.clear();
    if (completed != nullptr) completed->clear();
    if (selected.empty()) {
        return true;
    }

    bool restart_needed = false;
    std::size_t index = 0U;
    for (const ExternalUpdate &update : selected) {
        ++index;
        if (update.backend != "Cinnamon" ||
            descriptor_for(update.kind) == nullptr ||
            !safe_component(update.id) ||
            update.remote_revision <= 0) {
            error = "Invalid Cinnamon Spice update selection.";
            return false;
        }

        if (!reviewed_spice_is_current(update, error)) {
            if (restart_needed) {
                std::string restart_error;
                if (!restart_cinnamon_if_needed(
                        true, restart_error) &&
                    !restart_error.empty()) {
                    error += " " + restart_error;
                }
            }
            return false;
        }

        gchar *temporary =
            g_dir_make_tmp(
                "infiltrator-software-spice-XXXXXX",
                nullptr);
        if (temporary == nullptr) {
            error = "Unable to create a temporary Cinnamon Spice directory.";
            return false;
        }
        const fs::path root(temporary);
        g_free(temporary);
        const fs::path archive = root / "spice.zip";
        const fs::path extracted = root / "payload";

        if (progress) {
            progress(
                "Cinnamon " + std::to_string(index) +
                "/" + std::to_string(selected.size()) +
                " • preparing " +
                (update.name.empty() ? update.id : update.name));
        }

        bool success =
            download_archive(
                update,
                index,
                selected.size(),
                archive,
                error,
                progress);
        if (success && progress) {
            progress(
                "Cinnamon " + std::to_string(index) +
                "/" + std::to_string(selected.size()) +
                " • verifying and extracting " + update.id);
        }
        if (success) {
            success =
                extract_zip_safely(
                    archive,
                    extracted,
                    error);
        }
        if (success) {
            restart_needed =
                restart_needed ||
                spice_is_enabled(update);
            if (progress) {
                progress(
                    "Cinnamon " + std::to_string(index) +
                    "/" + std::to_string(selected.size()) +
                    " • installing " + update.id);
            }
            success =
                install_extracted(
                    update,
                    extracted,
                    error);
        }

        std::error_code ec;
        fs::remove_all(root, ec);
        if (!success) {
            if (restart_needed) {
                std::string restart_error;
                if (!restart_cinnamon_if_needed(
                        true, restart_error) &&
                    !restart_error.empty()) {
                    error += " " + restart_error;
                }
            }
            return false;
        }
        if (completed != nullptr) {
            completed->push_back(update);
        }
    }

    if (!restart_cinnamon_if_needed(
            restart_needed,
            error)) {
        return false;
    }
    if (progress) {
        progress(
            "Cinnamon updates complete • " +
            std::to_string(selected.size()) +
            (selected.size() == 1U ? " item" : " items"));
    }
    return true;
}

} // namespace infiltrator::software
