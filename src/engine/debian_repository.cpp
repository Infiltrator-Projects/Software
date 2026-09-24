// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_repository.hpp"

#include "engine/debian_package_index.hpp"

#include <curl/curl.h>
#include <glib.h>
#include <lzma.h>
#include <zlib.h>
#include <infiltratr/posix.h>
#include <infiltratr/posix_io.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

std::string trim(const std::string_view value)
{
    std::size_t first = 0U;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::vector<std::string> split_words(const std::string_view value)
{
    std::istringstream input{std::string(value)};
    std::vector<std::string> result;
    std::string word;
    while (input >> word) {
        result.emplace_back(std::move(word));
    }
    return result;
}

std::uint64_t parse_u64(const std::string_view value)
{
    std::uint64_t parsed_value = 0U;
    const auto parsed =
        std::from_chars(
            value.data(), value.data() + value.size(), parsed_value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size()) {
        return 0U;
    }
    return parsed_value;
}

std::string lower_ascii(std::string value)
{
    for (char &character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string join_uri(
    const std::string_view base,
    const std::string_view relative)
{
    std::string result(base);
    if (!result.empty() && result.back() != '/') {
        result.push_back('/');
    }
    std::size_t first = 0U;
    while (first < relative.size() && relative[first] == '/') {
        ++first;
    }
    result.append(relative.substr(first));
    return result;
}

std::string distribution_root(const DebianRepositorySource &source)
{
    if (!source.suite.empty() && source.suite.back() == '/') {
        return join_uri(source.uri, source.suite);
    }
    return join_uri(source.uri, "dists/" + source.suite + "/");
}

std::size_t curl_writer(
    char *data,
    const std::size_t size,
    const std::size_t count,
    void *user_data)
{
    const std::size_t bytes = size * count;
    auto *output = static_cast<std::string *>(user_data);
    output->append(data, bytes);
    return bytes;
}

bool download(
    const std::string &uri,
    std::string &content,
    std::string &error)
{
    content.clear();
    error.clear();
    CURL *handle = curl_easy_init();
    if (handle == nullptr) {
        error = "Unable to initialise repository downloader.";
        return false;
    }

    std::array<char, CURL_ERROR_SIZE> curl_error{};
    curl_easy_setopt(handle, CURLOPT_URL, uri.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "Infiltrator-Software/0.4");
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, curl_error.data());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_writer);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &content);

    const CURLcode status = curl_easy_perform(handle);
    curl_easy_cleanup(handle);

    if (status != CURLE_OK) {
        error = curl_error[0] != '\0'
            ? std::string(curl_error.data())
            : std::string(curl_easy_strerror(status));
        content.clear();
        return false;
    }
    return true;
}

std::string sha256_hex(const std::string_view content)
{
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (checksum == nullptr) {
        return {};
    }
    g_checksum_update(
        checksum,
        reinterpret_cast<const guchar *>(content.data()),
        content.size());
    const char *digest = g_checksum_get_string(checksum);
    const std::string result = digest == nullptr ? "" : digest;
    g_checksum_free(checksum);
    return result;
}

bool verify_payload(
    const std::string_view payload,
    const DebianReleaseEntry &entry,
    std::string &error)
{
    if (payload.size() != entry.size_bytes) {
        std::ostringstream message;
        message << "Repository index size mismatch for "
                << entry.path << ": expected "
                << entry.size_bytes << ", received "
                << payload.size() << ".";
        error = message.str();
        return false;
    }

    const std::string digest = sha256_hex(payload);
    if (digest.empty() ||
        lower_ascii(digest) != lower_ascii(entry.sha256)) {
        error =
            "Repository index SHA-256 mismatch for " + entry.path + ".";
        return false;
    }
    return true;
}

void append_keyring_if_present(
    std::vector<std::string> &result,
    const std::filesystem::path &path)
{
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec) && !ec) {
        result.emplace_back(path.string());
    }
}

std::vector<std::string> default_keyrings()
{
    std::vector<std::string> result;
    append_keyring_if_present(result, "/etc/apt/trusted.gpg");
    append_keyring_if_present(
        result, "/usr/share/keyrings/debian-archive-keyring.gpg");
    append_keyring_if_present(
        result, "/usr/share/keyrings/ubuntu-archive-keyring.gpg");

    const std::filesystem::path directory{"/etc/apt/trusted.gpg.d"};
    std::error_code ec;
    if (std::filesystem::is_directory(directory, ec) && !ec) {
        for (const auto &entry :
             std::filesystem::directory_iterator(directory, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const std::string extension =
                entry.path().extension().string();
            if (extension == ".gpg" || extension == ".asc") {
                result.emplace_back(entry.path().string());
            }
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(
        std::unique(result.begin(), result.end()),
        result.end());
    return result;
}

bool write_all(const int fd, const std::string_view content)
{
    return infiltratr_write_full(fd, content.data(), content.size()) == 0;
}

struct TemporaryFile {
    std::string path;

    TemporaryFile() = default;
    TemporaryFile(const TemporaryFile &) = delete;
    TemporaryFile &operator=(const TemporaryFile &) = delete;

    TemporaryFile(TemporaryFile &&other) noexcept
        : path(std::move(other.path))
    {
        other.path.clear();
    }

    TemporaryFile &operator=(TemporaryFile &&other) noexcept
    {
        if (this == &other) {
            return *this;
        }
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
        path = std::move(other.path);
        other.path.clear();
        return *this;
    }

    ~TemporaryFile()
    {
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }
};

bool create_temporary_file(
    std::string_view content,
    TemporaryFile &file,
    std::string &error);

std::uint32_t crc24(const std::string_view content)
{
    std::uint32_t crc = 0xB704CEU;
    for (const unsigned char byte : content) {
        crc ^= static_cast<std::uint32_t>(byte) << 16U;
        for (unsigned int bit = 0U; bit < 8U; ++bit) {
            crc <<= 1U;
            if ((crc & 0x1000000U) != 0U) {
                crc ^= 0x1864CFBU;
            }
        }
    }
    return crc & 0xFFFFFFU;
}

bool valid_base64_line(const std::string_view line)
{
    if (line.empty()) {
        return false;
    }
    for (const unsigned char ch : line) {
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '+' || ch == '/' || ch == '=') {
            continue;
        }
        return false;
    }
    return true;
}

bool dearmor_public_keys(
    const std::string_view armored,
    std::string &binary,
    std::string &error)
{
    static constexpr std::string_view begin_marker =
        "-----BEGIN PGP PUBLIC KEY BLOCK-----";
    static constexpr std::string_view end_marker =
        "-----END PGP PUBLIC KEY BLOCK-----";

    binary.clear();
    error.clear();
    std::size_t cursor = 0U;
    bool found = false;

    while (cursor < armored.size()) {
        const std::size_t begin = armored.find(begin_marker, cursor);
        if (begin == std::string_view::npos) {
            break;
        }
        found = true;

        std::size_t position = armored.find('\n', begin);
        if (position == std::string_view::npos) {
            error = "ASCII-armored repository key has no body.";
            binary.clear();
            return false;
        }
        ++position;

        bool body_started = false;
        std::string payload;
        std::string checksum;
        std::size_t end_after = std::string_view::npos;

        while (position <= armored.size()) {
            const std::size_t newline = armored.find('\n', position);
            const std::size_t line_end =
                newline == std::string_view::npos
                    ? armored.size()
                    : newline;
            std::string line =
                trim(armored.substr(position, line_end - position));
            position =
                newline == std::string_view::npos
                    ? armored.size() + 1U
                    : newline + 1U;

            if (!body_started) {
                if (line.empty()) {
                    body_started = true;
                }
                continue;
            }

            if (line == end_marker) {
                end_after = position;
                break;
            }
            if (line.empty()) {
                continue;
            }
            if (line.front() == '=') {
                if (!checksum.empty() || line.size() != 5U ||
                    !valid_base64_line(line.substr(1U))) {
                    error =
                        "ASCII-armored repository key has an invalid CRC line.";
                    binary.clear();
                    return false;
                }
                checksum = line.substr(1U);
                continue;
            }
            if (!checksum.empty() || !valid_base64_line(line)) {
                error =
                    "ASCII-armored repository key contains invalid base64 data.";
                binary.clear();
                return false;
            }
            payload += line;
        }

        if (end_after == std::string_view::npos || payload.empty()) {
            error =
                "ASCII-armored repository key block is incomplete.";
            binary.clear();
            return false;
        }

        gsize decoded_size = 0U;
        guchar *decoded =
            g_base64_decode(payload.c_str(), &decoded_size);
        if (decoded == nullptr || decoded_size == 0U) {
            g_free(decoded);
            error =
                "Unable to decode ASCII-armored repository key.";
            binary.clear();
            return false;
        }

        const std::string_view decoded_view(
            reinterpret_cast<const char *>(decoded),
            static_cast<std::size_t>(decoded_size));

        if (!checksum.empty()) {
            gsize checksum_size = 0U;
            guchar *checksum_bytes =
                g_base64_decode(checksum.c_str(), &checksum_size);
            if (checksum_bytes == nullptr || checksum_size != 3U) {
                g_free(checksum_bytes);
                g_free(decoded);
                error =
                    "ASCII-armored repository key CRC is invalid.";
                binary.clear();
                return false;
            }

            const std::uint32_t expected =
                (static_cast<std::uint32_t>(checksum_bytes[0]) << 16U) |
                (static_cast<std::uint32_t>(checksum_bytes[1]) << 8U) |
                static_cast<std::uint32_t>(checksum_bytes[2]);
            g_free(checksum_bytes);

            if (crc24(decoded_view) != expected) {
                g_free(decoded);
                error =
                    "ASCII-armored repository key CRC check failed.";
                binary.clear();
                return false;
            }
        }

        binary.append(decoded_view);
        g_free(decoded);
        cursor = end_after;
    }

    if (!found || binary.empty()) {
        error =
            "ASCII-armored repository key contains no public key block.";
        binary.clear();
        return false;
    }
    return true;
}

bool read_keyring(
    const std::filesystem::path &path,
    std::string &content,
    std::string &error)
{
    std::error_code ec;
    const std::uintmax_t size =
        std::filesystem::file_size(path, ec);
    if (ec || size > 16U * 1024U * 1024U) {
        error =
            "Unable to read repository keyring " +
            path.string() + ".";
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error =
            "Unable to open repository keyring " +
            path.string() + ".";
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error =
            "Unable to finish reading repository keyring " +
            path.string() + ".";
        return false;
    }
    content = stream.str();
    return true;
}

bool prepare_gpgv_keyrings(
    const std::vector<std::string> &configured,
    std::vector<std::string> &prepared,
    std::vector<TemporaryFile> &temporary,
    std::string &error)
{
    prepared.clear();
    temporary.clear();
    error.clear();

    for (const std::string &keyring : configured) {
        const std::filesystem::path path(keyring);
        if (path.extension() != ".asc") {
            prepared.emplace_back(keyring);
            continue;
        }

        std::string armored;
        if (!read_keyring(path, armored, error)) {
            return false;
        }

        std::string binary;
        if (!dearmor_public_keys(armored, binary, error)) {
            error =
                "Unable to prepare repository keyring " +
                path.string() + ": " + error;
            return false;
        }

        TemporaryFile file;
        if (!create_temporary_file(binary, file, error)) {
            error =
                "Unable to materialise repository keyring " +
                path.string() + ": " + error;
            return false;
        }
        prepared.emplace_back(file.path);
        temporary.emplace_back(std::move(file));
    }

    return true;
}

bool create_temporary_file(
    const std::string_view content,
    TemporaryFile &file,
    std::string &error)
{
    gchar *name = nullptr;
    GError *gerror = nullptr;
    const int fd = g_file_open_tmp(
        "infiltrator-software-repository-XXXXXX",
        &name,
        &gerror);
    if (fd < 0) {
        error = gerror != nullptr
            ? gerror->message
            : "Unable to create signature verification file.";
        if (gerror != nullptr) {
            g_error_free(gerror);
        }
        return false;
    }

    file.path = name == nullptr ? "" : name;
    g_free(name);
    const bool written = write_all(fd, content);
    const int close_status = close(fd);
    if (!written || close_status != 0) {
        error = "Unable to write signature verification file.";
        return false;
    }
    return true;
}

bool verify_gpg(
    const std::string_view signed_content,
    const std::string_view detached_signature,
    const std::vector<std::string> &configured_keyrings,
    const bool detached,
    std::string &error)
{
    TemporaryFile content_file;
    if (!create_temporary_file(
            signed_content, content_file, error)) {
        return false;
    }

    TemporaryFile signature_file;
    if (detached &&
        !create_temporary_file(
            detached_signature, signature_file, error)) {
        return false;
    }

    std::vector<std::string> keyrings = configured_keyrings;
    if (keyrings.empty()) {
        keyrings = default_keyrings();
    }
    if (keyrings.empty()) {
        error =
            "Repository signature verification has no trusted keyring.";
        return false;
    }

    /*
     * APT accepts ASCII-armored Signed-By key files (the recommended Docker
     * configuration uses /etc/apt/keyrings/docker.asc), while gpgv expects
     * binary keyring material when --keyring is supplied.  Convert only the
     * verification copy in memory/a private temporary file; never rewrite the
     * administrator's configured key.
     */
    std::vector<std::string> prepared_keyrings;
    std::vector<TemporaryFile> temporary_keyrings;
    if (!prepare_gpgv_keyrings(
            keyrings,
            prepared_keyrings,
            temporary_keyrings,
            error)) {
        return false;
    }

    std::vector<std::string> arguments{"gpgv", "--quiet"};
    for (const std::string &keyring : prepared_keyrings) {
        arguments.emplace_back("--keyring");
        arguments.emplace_back(keyring);
    }
    if (detached) {
        arguments.emplace_back(signature_file.path);
        arguments.emplace_back(content_file.path);
    } else {
        arguments.emplace_back(content_file.path);
    }

    std::vector<gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (std::string &argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    gchar *standard_output = nullptr;
    gchar *standard_error = nullptr;
    gint wait_status = 0;
    GError *gerror = nullptr;
    const gboolean spawned = g_spawn_sync(
        nullptr,
        argv.data(),
        nullptr,
        G_SPAWN_SEARCH_PATH,
        nullptr,
        nullptr,
        &standard_output,
        &standard_error,
        &wait_status,
        &gerror);
    if (!spawned) {
        error = gerror != nullptr
            ? gerror->message
            : "Unable to run repository signature verification.";
        if (gerror != nullptr) {
            g_error_free(gerror);
        }
        g_free(standard_output);
        g_free(standard_error);
        return false;
    }

    GError *status_error = nullptr;
    const gboolean successful =
        g_spawn_check_wait_status(wait_status, &status_error);
    if (!successful) {
        const std::string detail =
            standard_error != nullptr
                ? trim(standard_error)
                : std::string{};
        error = detail.empty()
            ? "Repository signature verification failed."
            : "Repository signature verification failed: " + detail;
        if (status_error != nullptr) {
            g_error_free(status_error);
        }
        g_free(standard_output);
        g_free(standard_error);
        return false;
    }

    g_free(standard_output);
    g_free(standard_error);
    return true;
}

bool extract_inrelease(
    const std::string_view signed_document,
    std::string &release,
    std::string &error)
{
    release.clear();
    constexpr std::string_view begin =
        "-----BEGIN PGP SIGNED MESSAGE-----";
    constexpr std::string_view signature =
        "-----BEGIN PGP SIGNATURE-----";

    if (signed_document.rfind(begin, 0U) != 0U) {
        error = "Repository InRelease is not an OpenPGP clear-signed document.";
        return false;
    }

    const std::size_t header_end = signed_document.find("\n\n");
    if (header_end == std::string_view::npos) {
        error = "Repository InRelease has no clear-text payload.";
        return false;
    }

    std::size_t start = header_end + 2U;
    while (start < signed_document.size()) {
        const std::size_t newline = signed_document.find('\n', start);
        const std::size_t end =
            newline == std::string_view::npos
                ? signed_document.size()
                : newline;
        std::string_view line =
            signed_document.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        if (line == signature) {
            return true;
        }
        if (line.rfind("- ", 0U) == 0U) {
            line.remove_prefix(2U);
        }
        release.append(line);
        release.push_back('\n');
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1U;
    }

    error = "Repository InRelease signature block is missing.";
    release.clear();
    return false;
}

bool load_release(
    const DebianRepositorySource &source,
    const std::string &root,
    std::string &release,
    std::string &error)
{
    if (!source.verify_signatures) {
        return download(join_uri(root, "Release"), release, error);
    }

    std::string inrelease;
    std::string inrelease_error;
    if (download(
            join_uri(root, "InRelease"),
            inrelease,
            inrelease_error)) {
        if (!verify_gpg(
                inrelease, {}, source.keyrings, false, error)) {
            return false;
        }
        return extract_inrelease(inrelease, release, error);
    }

    std::string detached_signature;
    if (!download(join_uri(root, "Release"), release, error)) {
        error =
            "Unable to fetch signed repository Release metadata: " + error;
        return false;
    }
    if (!download(
            join_uri(root, "Release.gpg"),
            detached_signature,
            error)) {
        error =
            "Repository InRelease was unavailable and Release.gpg "
            "could not be fetched: " + error;
        return false;
    }
    return verify_gpg(
        release,
        detached_signature,
        source.keyrings,
        true,
        error);
}

bool decompress_gzip(
    const std::string_view input,
    const std::size_t maximum_output,
    std::string &output,
    std::string &error)
{
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        error = "Unable to initialise gzip decompressor.";
        return false;
    }

    stream.next_in = reinterpret_cast<Bytef *>(
        const_cast<char *>(input.data()));
    stream.avail_in = static_cast<uInt>(
        std::min<std::size_t>(
            input.size(),
            std::numeric_limits<uInt>::max()));

    std::array<char, 32768> buffer{};
    std::size_t consumed = 0U;
    output.clear();

    for (;;) {
        if (stream.avail_in == 0U && consumed < input.size()) {
            const std::size_t chunk =
                std::min<std::size_t>(
                    input.size() - consumed,
                    std::numeric_limits<uInt>::max());
            stream.next_in = reinterpret_cast<Bytef *>(
                const_cast<char *>(input.data() + consumed));
            stream.avail_in = static_cast<uInt>(chunk);
        }

        const std::size_t before = stream.avail_in;
        stream.next_out =
            reinterpret_cast<Bytef *>(buffer.data());
        stream.avail_out =
            static_cast<uInt>(buffer.size());

        const int status = inflate(&stream, Z_NO_FLUSH);
        consumed += before - stream.avail_in;
        const std::size_t produced =
            buffer.size() - stream.avail_out;
        if (produced > maximum_output - output.size()) {
            error =
                "Decompressed gzip repository index exceeds the signed size.";
            inflateEnd(&stream);
            output.clear();
            return false;
        }
        output.append(buffer.data(), produced);

        if (status == Z_STREAM_END) {
            inflateEnd(&stream);
            return true;
        }
        if (status != Z_OK) {
            error = "Unable to decompress gzip repository index.";
            inflateEnd(&stream);
            output.clear();
            return false;
        }
        if (stream.avail_in == 0U &&
            consumed >= input.size()) {
            error = "Truncated gzip repository index.";
            inflateEnd(&stream);
            output.clear();
            return false;
        }
    }
}

bool decompress_xz(
    const std::string_view input,
    const std::size_t maximum_output,
    std::string &output,
    std::string &error)
{
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_stream_decoder(
            &stream,
            UINT64_MAX,
            LZMA_CONCATENATED) != LZMA_OK) {
        error = "Unable to initialise xz decompressor.";
        return false;
    }

    stream.next_in =
        reinterpret_cast<const std::uint8_t *>(input.data());
    stream.avail_in = input.size();
    std::array<std::uint8_t, 32768> buffer{};
    output.clear();

    for (;;) {
        stream.next_out = buffer.data();
        stream.avail_out = buffer.size();
        const lzma_ret status =
            lzma_code(
                &stream,
                stream.avail_in == 0U
                    ? LZMA_FINISH
                    : LZMA_RUN);

        const std::size_t produced =
            buffer.size() - stream.avail_out;
        if (produced > maximum_output - output.size()) {
            error =
                "Decompressed xz repository index exceeds the signed size.";
            lzma_end(&stream);
            output.clear();
            return false;
        }
        output.append(
            reinterpret_cast<const char *>(buffer.data()),
            produced);

        if (status == LZMA_STREAM_END) {
            lzma_end(&stream);
            return true;
        }
        if (status != LZMA_OK) {
            error = "Unable to decompress xz repository index.";
            lzma_end(&stream);
            output.clear();
            return false;
        }
    }
}

bool decompress_index(
    const std::string_view path,
    const std::string_view input,
    const std::size_t maximum_output,
    std::string &output,
    std::string &error)
{
    if (path.size() >= 3U &&
        path.substr(path.size() - 3U) == ".gz") {
        return decompress_gzip(
            input, maximum_output, output, error);
    }
    if (path.size() >= 3U &&
        path.substr(path.size() - 3U) == ".xz") {
        return decompress_xz(
            input, maximum_output, output, error);
    }
    if (input.size() > maximum_output) {
        error = "Repository package index exceeds the signed size.";
        output.clear();
        return false;
    }
    output.assign(input);
    return true;
}

const DebianReleaseEntry *preferred_index(
    const DebianReleaseMetadata &release,
    const std::string &base_path)
{
    static constexpr std::array<std::string_view, 3> suffixes{
        ".xz", ".gz", ""};
    for (const std::string_view suffix : suffixes) {
        const std::string path = base_path + std::string(suffix);
        if (const DebianReleaseEntry *entry = release.find(path);
            entry != nullptr) {
            return entry;
        }
    }
    return nullptr;
}

std::string safe_cache_name(std::string value)
{
    for (char &character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0 &&
            character != '-' && character != '_' &&
            character != '.') {
            character = '_';
        }
    }
    return value.empty() ? "repository" : value;
}

bool write_atomic(
    const std::filesystem::path &path,
    const std::string_view content,
    std::string &error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error =
            "Unable to create repository cache directory: " +
            ec.message();
        return false;
    }

    const std::string destination = path.string();
    const int status = infiltratr_atomic_file_write_bytes(
        destination.c_str(),
        INFILTRATR_ATOMIC_FILE_PRIVATE,
        content.data(),
        content.size());
    if (status != 0) {
        error =
            "Unable to durably publish repository cache file: " +
            destination + " (" + std::to_string(status) + ").";
        return false;
    }
    return true;
}

} // namespace

DebianReleaseMetadata DebianReleaseMetadata::parse(
    const std::string_view content,
    std::string &error)
{
    error.clear();
    DebianReleaseMetadata result;
    std::istringstream input{std::string(content)};
    std::string line;
    bool in_sha256 = false;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (in_sha256) {
            if (!line.empty() &&
                std::isspace(
                    static_cast<unsigned char>(line.front())) != 0) {
                std::istringstream entry(trim(line));
                DebianReleaseEntry release_entry;
                std::string size_text;
                if (entry >> release_entry.sha256
                          >> size_text
                          >> release_entry.path) {
                    release_entry.sha256 =
                        lower_ascii(release_entry.sha256);
                    release_entry.size_bytes =
                        parse_u64(size_text);
                    if (release_entry.sha256.size() == 64U &&
                        !release_entry.path.empty()) {
                        result.sha256_entries.emplace_back(
                            std::move(release_entry));
                    }
                }
                continue;
            }
            in_sha256 = false;
        }

        if (line == "SHA256:") {
            in_sha256 = true;
            continue;
        }

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string key =
            trim(std::string_view(line).substr(0U, colon));
        const std::string value =
            trim(std::string_view(line).substr(colon + 1U));

        if (key == "Suite") {
            result.suite = value;
        } else if (key == "Codename") {
            result.codename = value;
        } else if (key == "Architectures") {
            result.architectures = split_words(value);
        } else if (key == "Components") {
            result.components = split_words(value);
        }
    }

    if (result.sha256_entries.empty()) {
        error = "Repository Release metadata contains no SHA256 index.";
    }
    return result;
}

const DebianReleaseEntry *DebianReleaseMetadata::find(
    const std::string_view path) const noexcept
{
    for (const DebianReleaseEntry &entry : sha256_entries) {
        if (entry.path == path) {
            return &entry;
        }
    }
    return nullptr;
}

DebianRepositorySnapshot DebianRepositoryRefresh::refresh(
    const DebianRepositorySource &source,
    const std::string_view architecture,
    const std::string &cache_directory,
    std::string &error)
{
    error.clear();
    DebianRepositorySnapshot snapshot;
    snapshot.source_id = source.id;
    snapshot.suite = source.suite;

    if (source.uri.empty() || source.suite.empty()) {
        error = "Repository URI and suite are required.";
        return snapshot;
    }
    if (architecture.empty()) {
        error = "Repository target architecture is required.";
        return snapshot;
    }

    const std::string root = distribution_root(source);
    std::string release_content;
    if (!load_release(
            source, root, release_content, error)) {
        return snapshot;
    }

    DebianReleaseMetadata release =
        DebianReleaseMetadata::parse(release_content, error);
    if (!error.empty()) {
        return snapshot;
    }

    std::vector<std::string> components = source.components;
    if (components.empty()) {
        components = release.components;
    }

    std::vector<std::string> base_indexes;
    if (!source.suite.empty() && source.suite.back() == '/') {
        base_indexes.emplace_back("Packages");
    } else {
        if (components.empty()) {
            error =
                "Repository has no configured or advertised components.";
            return snapshot;
        }
        for (const std::string &component : components) {
            base_indexes.emplace_back(
                component + "/binary-" +
                std::string(architecture) + "/Packages");
        }
    }

    std::vector<std::string> cached_contents;
    cached_contents.reserve(base_indexes.size());

    for (const std::string &base_index : base_indexes) {
        const DebianReleaseEntry *entry =
            preferred_index(release, base_index);
        const DebianReleaseEntry *uncompressed_entry =
            release.find(base_index);
        if (entry == nullptr || uncompressed_entry == nullptr) {
            error =
                "Repository Release metadata has no complete package index "
                "integrity record for " + base_index + ".";
            return {};
        }
        if (uncompressed_entry->size_bytes >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            error =
                "Repository package index is too large for this platform: " +
                base_index + ".";
            return {};
        }

        std::string compressed;
        if (!download(
                join_uri(root, entry->path),
                compressed,
                error)) {
            error =
                "Unable to fetch repository package index " +
                entry->path + ": " + error;
            return {};
        }
        if (!verify_payload(compressed, *entry, error)) {
            return {};
        }

        std::string packages_text;
        if (!decompress_index(
                entry->path,
                compressed,
                static_cast<std::size_t>(
                    uncompressed_entry->size_bytes),
                packages_text,
                error)) {
            return {};
        }
        if (!verify_payload(
                packages_text, *uncompressed_entry, error)) {
            return {};
        }

        std::string parse_error;
        std::vector<DebianPackageVersion> packages =
            DebianPackageIndex::parse(
                packages_text,
                source.id,
                parse_error);
        if (!parse_error.empty()) {
            error =
                "Unable to parse repository package index " +
                entry->path + ": " + parse_error;
            return {};
        }

        snapshot.packages.insert(
            snapshot.packages.end(),
            std::make_move_iterator(packages.begin()),
            std::make_move_iterator(packages.end()));
        snapshot.verified_indexes.emplace_back(entry->path);
        cached_contents.emplace_back(std::move(packages_text));
    }

    if (!cache_directory.empty()) {
        const std::filesystem::path root_cache =
            std::filesystem::path(cache_directory) /
            safe_cache_name(source.id.empty() ? source.uri : source.id);

        if (!write_atomic(
                root_cache / "Release",
                release_content,
                error)) {
            return {};
        }

        for (std::size_t index = 0U;
             index < snapshot.verified_indexes.size();
             ++index) {
            const std::filesystem::path cache_path =
                root_cache /
                (safe_cache_name(snapshot.verified_indexes[index]) +
                 ".uncompressed");
            if (!write_atomic(
                    cache_path,
                    cached_contents[index],
                    error)) {
                return {};
            }
        }
    }

    std::sort(
        snapshot.packages.begin(),
        snapshot.packages.end(),
        [](const DebianPackageVersion &left,
           const DebianPackageVersion &right) {
            if (left.package != right.package) {
                return left.package < right.package;
            }
            if (left.architecture != right.architecture) {
                return left.architecture < right.architecture;
            }
            return left.version < right.version;
        });

    return snapshot;
}

} // namespace infiltrator::software
