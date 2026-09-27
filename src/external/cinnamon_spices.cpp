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
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kSpiceRoot =
    "https://cinnamon-spices.linuxmint.com";
constexpr std::uint64_t kMaxArchiveBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxExpandedBytes = 512ULL * 1024ULL * 1024ULL;

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

size_t append_to_string(
    char *data,
    const size_t size,
    const size_t count,
    void *user_data)
{
    auto *text = static_cast<std::string *>(user_data);
    const size_t bytes = size * count;
    text->append(data, bytes);
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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &text);

    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        error =
            "Unable to fetch Cinnamon Spice metadata: " +
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
            fs::permissions(
                target,
                static_cast<fs::perms>(permissions),
                fs::perm_options::replace,
                ec);
            ec.clear();
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
};

size_t write_download(
    char *data,
    const size_t size,
    const size_t count,
    void *user_data)
{
    auto *context =
        static_cast<DownloadContext *>(user_data);
    return std::fwrite(
        data,
        size,
        count,
        context->file);
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
        count
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
            (result == CURLE_OK
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
            G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                G_SUBPROCESS_FLAGS_STDERR_PIPE,
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

bool compile_translations(
    const fs::path &source,
    const std::string &identity,
    std::string &error)
{
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
            error = "Unable to create the Cinnamon Spice locale directory.";
            return false;
        }
        if (!run_command(
                {"msgfmt", "-c",
                 entry.path().string(),
                 "-o", destination.string()},
                error)) {
            return false;
        }
    }
    return true;
}

bool remove_previous_copies(
    const ExternalUpdate &update,
    std::string &error)
{
    std::error_code ec;
    for (const fs::path &folder :
         user_install_folders(update.kind)) {
        fs::remove_all(folder / update.id, ec);
        if (ec) {
            error =
                "Unable to remove the previous Cinnamon Spice copy: " +
                ec.message();
            return false;
        }
        if (update.kind == ExternalUpdateKind::nemo_action) {
            fs::remove(folder / (update.id + ".nemo_action"), ec);
            if (ec) {
                error =
                    "Unable to replace the previous Nemo action: " +
                    ec.message();
                return false;
            }
        }
    }
    return true;
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
    const fs::path destination_root = folders.front();
    const fs::path source = extracted / update.id;
    if (!fs::is_directory(source)) {
        error =
            "Downloaded Cinnamon Spice does not contain its expected " +
            update.id + " directory.";
        return false;
    }

    if (!compile_translations(source, update.id, error)) {
        return false;
    }
    if (!remove_previous_copies(update, error)) {
        return false;
    }

    std::error_code ec;
    fs::create_directories(destination_root, ec);
    if (ec) {
        error = "Unable to create the Cinnamon Spice installation directory.";
        return false;
    }

    if (update.kind == ExternalUpdateKind::nemo_action) {
        for (const fs::directory_entry &entry :
             fs::directory_iterator(extracted, ec)) {
            if (ec) {
                error = "Unable to enumerate the Nemo action payload.";
                return false;
            }
            const fs::path destination =
                destination_root / entry.path().filename();
            fs::copy(
                entry.path(),
                destination,
                fs::copy_options::recursive |
                    fs::copy_options::overwrite_existing,
                ec);
            if (ec) {
                error =
                    "Unable to install the Nemo action payload: " +
                    ec.message();
                return false;
            }
        }
    } else {
        fs::copy(
            source,
            destination_root / update.id,
            fs::copy_options::recursive |
                fs::copy_options::overwrite_existing,
            ec);
        if (ec) {
            error =
                "Unable to install Cinnamon Spice " +
                update.id + ": " + ec.message();
            return false;
        }
    }

    const fs::path metadata =
        destination_root / update.id / "metadata.json";
    if (!write_revision(
            metadata,
            update.remote_revision,
            error)) {
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

void restart_cinnamon_if_needed(const bool needed)
{
    if (!needed) {
        return;
    }
    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    if (desktop == nullptr ||
        (std::string_view(desktop) != "Cinnamon" &&
         std::string_view(desktop) != "X-Cinnamon")) {
        return;
    }
    gchar *command =
        g_find_program_in_path("cinnamon-dbus-command");
    if (command == nullptr) {
        return;
    }
    g_free(command);

    std::string ignored;
    (void)run_command(
        {"cinnamon-dbus-command", "RestartCinnamon", "0"},
        ignored);
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
    ExternalProgressCallback progress)
{
    error.clear();
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
            return false;
        }
    }

    restart_cinnamon_if_needed(restart_needed);
    if (progress) {
        progress(
            "Cinnamon updates complete • " +
            std::to_string(selected.size()) +
            (selected.size() == 1U ? " item" : " items"));
    }
    return true;
}

} // namespace infiltrator::software
