// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue/repository_catalogue.hpp"

#include <infiltratr/posix.h>

#include <curl/curl.h>
#include <json-glib/json-glib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

constexpr std::string_view kEndpoint =
    "https://infiltrator-projects.github.io/"
    "Infiltrator-Repository/catalogue/apps.json";
constexpr std::string_view kRepositoryRoot =
    "https://infiltrator-projects.github.io/Infiltrator-Repository/";
constexpr std::size_t kCatalogueLimit = 4U * 1024U * 1024U;
constexpr std::size_t kIconLimit = 2U * 1024U * 1024U;

struct CurlRuntime final {
    CurlRuntime()
        : ready(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK)
    {
    }

    ~CurlRuntime()
    {
        if (ready) {
            curl_global_cleanup();
        }
    }

    bool ready{false};
};

CurlRuntime curl_runtime;

struct DownloadBuffer {
    std::string data;
    std::size_t maximum{0};
};

std::size_t curl_write(
    char *contents,
    const std::size_t size,
    const std::size_t count,
    void *user_data)
{
    auto *buffer = static_cast<DownloadBuffer *>(user_data);
    if (buffer == nullptr || contents == nullptr || size == 0U) {
        return 0U;
    }

    if (count > std::numeric_limits<std::size_t>::max() / size) {
        return 0U;
    }
    const std::size_t bytes = size * count;
    if (bytes > buffer->maximum ||
        buffer->data.size() > buffer->maximum - bytes) {
        return 0U;
    }

    buffer->data.append(contents, bytes);
    return bytes;
}

std::string json_string(JsonObject *object, const char *member)
{
    if (object == nullptr || member == nullptr ||
        !json_object_has_member(object, member)) {
        return {};
    }

    JsonNode *node = json_object_get_member(object, member);
    if (node == nullptr || !JSON_NODE_HOLDS_VALUE(node)) {
        return {};
    }

    const char *value = json_node_get_string(node);
    return value == nullptr ? std::string{} : std::string(value);
}

std::uint64_t json_u64(JsonObject *object, const char *member)
{
    if (object == nullptr || member == nullptr ||
        !json_object_has_member(object, member)) {
        return 0U;
    }

    JsonNode *node = json_object_get_member(object, member);
    if (node == nullptr || !JSON_NODE_HOLDS_VALUE(node)) {
        return 0U;
    }

    const gint64 value = json_node_get_int(node);
    return value < 0 ? 0U : static_cast<std::uint64_t>(value);
}

bool safe_relative_icon(const std::string_view path) noexcept
{
    return path.rfind("catalogue/icons/", 0U) == 0U &&
           path.find("..") == std::string_view::npos &&
           path.find(':') == std::string_view::npos &&
           path.find('\\') == std::string_view::npos;
}

bool valid_sha256(const std::string_view digest) noexcept
{
    return digest.size() == 64U &&
           std::all_of(
               digest.begin(), digest.end(),
               [](const unsigned char value) {
                   return std::isxdigit(value) != 0;
               });
}

std::string lower_copy(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    return value;
}

std::string sha256(const std::string_view data)
{
    gchar *digest = g_compute_checksum_for_data(
        G_CHECKSUM_SHA256,
        reinterpret_cast<const guchar *>(data.data()),
        data.size());
    if (digest == nullptr) {
        return {};
    }

    std::string result(digest);
    g_free(digest);
    return result;
}

bool directory_ready(const std::string &path)
{
    gchar *directory = g_path_get_dirname(path.c_str());
    if (directory == nullptr) {
        return false;
    }
    const bool ready = g_mkdir_with_parents(directory, 0700) == 0;
    g_free(directory);
    return ready;
}

std::string icon_extension(const std::string &url)
{
    const std::filesystem::path path(url);
    std::string extension = lower_copy(path.extension().string());
    if (extension == ".svg" || extension == ".png" ||
        extension == ".webp" || extension == ".jpg" ||
        extension == ".jpeg") {
        return extension;
    }
    return ".img";
}

} // namespace

RepositoryCatalogue::RepositoryCatalogue()
    : endpoint_(kEndpoint),
      repository_root_(kRepositoryRoot)
{
}

std::string_view RepositoryCatalogue::name() const noexcept
{
    return "Infiltrator Repository";
}

CatalogueSnapshot RepositoryCatalogue::refresh(std::string &error)
{
    error.clear();
    CatalogueSnapshot snapshot;
    snapshot.source = std::string(name());

    std::string document;
    std::string live_error;
    bool live = download(endpoint_, kCatalogueLimit, document, live_error);

    if (!live) {
        if (!read_file(catalogue_cache_path(), document)) {
            error = live_error.empty()
                ? "The Infiltrator catalogue is unavailable."
                : live_error;
            return snapshot;
        }
        snapshot.from_cache = true;
    }

    std::string parse_error;
    snapshot.records =
        parse_document(document, repository_root_, parse_error);
    if (snapshot.records.empty() && !parse_error.empty()) {
        error = parse_error;
        return snapshot;
    }

    if (live) {
        std::string cache_error;
        if (!write_file(catalogue_cache_path(), document, cache_error) &&
            error.empty()) {
            error = cache_error;
        }
    } else if (!live_error.empty()) {
        error = "Live catalogue unavailable; using verified cached metadata. " +
                live_error;
    }

    for (PackageRecord &record : snapshot.records) {
        std::string icon_error;
        if (!cache_icon(record, icon_error) &&
            error.empty() && !icon_error.empty()) {
            error = icon_error;
        }
    }

    return snapshot;
}

std::vector<PackageRecord> RepositoryCatalogue::parse_document(
    const std::string_view document,
    const std::string_view repository_root,
    std::string &error)
{
    error.clear();
    std::vector<PackageRecord> records;

    JsonParser *parser = json_parser_new();
    if (parser == nullptr) {
        error = "Unable to create the catalogue parser.";
        return records;
    }

    GError *parse_error = nullptr;
    const gboolean loaded = json_parser_load_from_data(
        parser,
        document.data(),
        static_cast<gssize>(document.size()),
        &parse_error);
    if (!loaded) {
        error = parse_error != nullptr
            ? parse_error->message
            : "The catalogue is not valid JSON.";
        g_clear_error(&parse_error);
        g_object_unref(parser);
        return records;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (root == nullptr || !JSON_NODE_HOLDS_ARRAY(root)) {
        error = "The catalogue root must be an array.";
        g_object_unref(parser);
        return records;
    }

    JsonArray *array = json_node_get_array(root);
    const guint length = json_array_get_length(array);
    records.reserve(static_cast<std::size_t>(length));

    for (guint index = 0U; index < length; ++index) {
        JsonObject *object = json_array_get_object_element(array, index);
        if (object == nullptr) {
            error = "The catalogue contains a non-object entry.";
            records.clear();
            break;
        }

        PackageRecord record;
        record.id = json_string(object, "id");
        record.name = json_string(object, "name");
        record.package_name = json_string(object, "package");
        record.category = json_string(object, "category");
        record.description = json_string(object, "description");
        record.summary = json_string(object, "package_description");
        record.available_version = json_string(object, "version");
        record.architecture = json_string(object, "architecture");
        record.publisher = json_string(object, "maintainer");
        record.asset = json_string(object, "asset");
        record.package_sha256 =
            lower_copy(json_string(object, "sha256"));
        record.release_url = json_string(object, "release_url");
        record.source_url = json_string(object, "source_url");
        record.icon_name = json_string(object, "icon");
        record.icon_sha256 =
            lower_copy(json_string(object, "icon_sha256"));
        record.published_at = json_string(object, "published_at");
        record.download_size_bytes =
            json_u64(object, "download_size");
        record.source = "Infiltrator Repository";
        record.channel = Channel::beta;
        record.kind = PackageKind::application;
        record.state = InstallState::not_installed;

        const std::string relative_icon =
            json_string(object, "icon_url");
        if (!relative_icon.empty()) {
            if (!safe_relative_icon(relative_icon) ||
                !valid_sha256(record.icon_sha256)) {
                error =
                    "The catalogue contains an unsafe or unverifiable icon record.";
                records.clear();
                break;
            }
            record.icon_url =
                std::string(repository_root) + relative_icon;
        }

        if (!valid_identity(record) ||
            record.package_name.empty() ||
            record.available_version.empty() ||
            record.category.empty() ||
            !valid_sha256(record.package_sha256)) {
            error =
                "The catalogue contains an incomplete application identity.";
            records.clear();
            break;
        }

        records.emplace_back(std::move(record));
    }

    g_object_unref(parser);

    std::sort(
        records.begin(), records.end(),
        [](const PackageRecord &left, const PackageRecord &right) {
            return left.name < right.name;
        });
    return records;
}

bool RepositoryCatalogue::download(
    const std::string_view url,
    const std::size_t maximum_bytes,
    std::string &body,
    std::string &error)
{
    body.clear();
    error.clear();

    if (!curl_runtime.ready) {
        error = "The HTTPS client could not be initialized.";
        return false;
    }
    if (url.rfind("https://", 0U) != 0U) {
        error = "Catalogue downloads require HTTPS.";
        return false;
    }

    CURL *handle = curl_easy_init();
    if (handle == nullptr) {
        error = "Unable to create the HTTPS request.";
        return false;
    }

    DownloadBuffer buffer;
    buffer.maximum = maximum_bytes;
    const std::string url_text(url);

    curl_easy_setopt(handle, CURLOPT_URL, url_text.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "Infiltrator-Software/0.2");
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 3L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif

    const CURLcode result = curl_easy_perform(handle);
    if (result != CURLE_OK) {
        error = std::string("Catalogue HTTPS request failed: ") +
                curl_easy_strerror(result);
        curl_easy_cleanup(handle);
        return false;
    }

    curl_easy_cleanup(handle);
    body = std::move(buffer.data);
    return !body.empty();
}

std::string RepositoryCatalogue::cache_root()
{
    const char *root = g_get_user_cache_dir();
    if (root == nullptr || *root == '\0') {
        return {};
    }
    return (std::filesystem::path(root) /
            "infiltrator" / "software" / "catalogue").string();
}

std::string RepositoryCatalogue::catalogue_cache_path()
{
    const std::string root = cache_root();
    if (root.empty()) {
        return {};
    }
    return (std::filesystem::path(root) / "apps.json").string();
}

std::string RepositoryCatalogue::icon_cache_path(
    const PackageRecord &record)
{
    const std::string root = cache_root();
    if (root.empty() || !valid_sha256(record.icon_sha256)) {
        return {};
    }
    return (
        std::filesystem::path(root) / "icons" /
        (record.icon_sha256 + icon_extension(record.icon_url))).string();
}

bool RepositoryCatalogue::cache_icon(
    PackageRecord &record,
    std::string &error)
{
    error.clear();
    if (record.icon_url.empty() || record.icon_sha256.empty()) {
        return true;
    }

    const std::string path = icon_cache_path(record);
    if (path.empty()) {
        error = "Unable to determine the icon cache path.";
        return false;
    }

    std::string existing;
    if (read_file(path, existing) &&
        lower_copy(sha256(existing)) == record.icon_sha256) {
        record.cached_icon_path = path;
        return true;
    }

    std::string body;
    if (!download(record.icon_url, kIconLimit, body, error)) {
        return false;
    }
    if (lower_copy(sha256(body)) != record.icon_sha256) {
        error = "A downloaded application icon failed SHA-256 verification.";
        return false;
    }

    if (!write_file(path, body, error)) {
        return false;
    }

    record.cached_icon_path = path;
    return true;
}

bool RepositoryCatalogue::read_file(
    const std::string &path,
    std::string &content)
{
    content.clear();
    if (path.empty()) {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    content.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool RepositoryCatalogue::write_file(
    const std::string &path,
    const std::string_view content,
    std::string &error)
{
    error.clear();
    if (path.empty() || !directory_ready(path)) {
        error = "Unable to prepare the catalogue cache directory.";
        return false;
    }

    if (!infiltratr_atomic_file_write_bytes(
            path.c_str(),
            INFILTRATR_ATOMIC_FILE_PRIVATE,
            content.data(),
            content.size())) {
        error = "Unable to write the catalogue cache atomically.";
        return false;
    }
    return true;
}

} // namespace infiltrator::software
