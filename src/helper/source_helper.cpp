// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/source_mutation.hpp"

#include <cerrno>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

constexpr std::uintmax_t kMaximumSourceFileBytes = 16U * 1024U * 1024U;

bool safe_name(std::string_view value)
{
    if (value.empty() || value.size() > 64U) {
        return false;
    }
    for (const unsigned char ch : value) {
        if (!(std::isalnum(ch) != 0 || ch == '-' || ch == '_')) {
            return false;
        }
    }
    return true;
}

bool safe_token_list(std::string_view value)
{
    if (value.empty() || value.size() > 512U) {
        return false;
    }
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) != 0 ||
            ch == '-' || ch == '_' || ch == '.' || ch == '+' ||
            ch == '/' || ch == ':' || ch == ' ' || ch == '\t') {
            continue;
        }
        return false;
    }
    return true;
}

bool https_uri(std::string_view value)
{
    if (value.rfind("https://", 0U) != 0U ||
        value.size() > 2048U) {
        return false;
    }

    for (const unsigned char ch : value) {
        if (std::isspace(ch) != 0) {
            return false;
        }
    }
    return true;
}

bool valid_signed_by(std::string_view value)
{
    if (value.empty()) {
        return true;
    }
    if (value.find('\n') != std::string_view::npos ||
        value.find('\r') != std::string_view::npos) {
        return false;
    }

    const std::filesystem::path path(value);
    const std::string text = path.lexically_normal().string();
    if (text.rfind("/etc/apt/keyrings/", 0U) != 0U &&
        text.rfind("/usr/share/keyrings/", 0U) != 0U) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::string normalise_name(std::string value)
{
    for (char &ch : value) {
        if (ch == '_') {
            ch = '-';
        } else {
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    return value;
}

bool safe_existing_apt_source(
    const std::filesystem::path &path,
    struct stat &metadata)
{
    const std::filesystem::path normalised =
        path.lexically_normal();
    if (normalised != path) {
        return false;
    }

    const bool main_list =
        normalised == std::filesystem::path("/etc/apt/sources.list");
    const bool source_directory_file =
        normalised.parent_path() ==
            std::filesystem::path("/etc/apt/sources.list.d") &&
        (normalised.extension() == ".list" ||
         normalised.extension() == ".sources");
    if (!main_list && !source_directory_file) {
        return false;
    }

    if (lstat(normalised.c_str(), &metadata) != 0 ||
        !S_ISREG(metadata.st_mode)) {
        return false;
    }
    return true;
}

bool read_text(
    const std::filesystem::path &path,
    std::string &content)
{
    std::error_code ec;
    const std::uintmax_t size =
        std::filesystem::file_size(path, ec);
    if (ec || size > kMaximumSourceFileBytes) {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return false;
    }
    content = stream.str();
    return true;
}

bool write_atomic(
    const std::filesystem::path &path,
    const std::string &content,
    const struct stat *preserve = nullptr)
{
    const std::filesystem::path directory = path.parent_path();
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return false;
    }

    std::string temporary =
        (directory / ".infiltrator-software-source-XXXXXX").string();
    std::string mutable_name = temporary;
    mutable_name.push_back('\0');

    const int fd = mkstemp(mutable_name.data());
    if (fd < 0) {
        return false;
    }

    const char *data = content.data();
    std::size_t remaining = content.size();
    bool ok = true;

    while (remaining > 0U) {
        const ssize_t written = write(fd, data, remaining);
        if (written > 0) {
            data += written;
            remaining -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        ok = false;
        break;
    }

    if (preserve != nullptr) {
        if (fchown(fd, preserve->st_uid, preserve->st_gid) != 0) {
            ok = false;
        }
        if (fchmod(fd, preserve->st_mode & 07777) != 0) {
            ok = false;
        }
    } else if (fchmod(fd, 0644) != 0) {
        ok = false;
    }

    if (ok && fsync(fd) != 0) {
        ok = false;
    }
    if (close(fd) != 0) {
        ok = false;
    }

    const std::filesystem::path temp_path(mutable_name.data());
    if (!ok) {
        unlink(temp_path.c_str());
        return false;
    }

    if (rename(temp_path.c_str(), path.c_str()) != 0) {
        unlink(temp_path.c_str());
        return false;
    }

    const int dirfd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (dirfd >= 0) {
        (void)fsync(dirfd);
        (void)close(dirfd);
    }

    return true;
}

bool parse_index(
    const std::string_view value,
    std::size_t &result)
{
    result = 0U;
    if (value.empty()) {
        return false;
    }
    unsigned long long parsed = 0U;
    const auto converted =
        std::from_chars(
            value.data(),
            value.data() + value.size(),
            parsed);
    if (converted.ec != std::errc{} ||
        converted.ptr != value.data() + value.size() ||
        parsed == 0U ||
        parsed >
            static_cast<unsigned long long>(
                std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    result = static_cast<std::size_t>(parsed);
    return true;
}

int set_apt_source_enabled(
    const char *path_text,
    const char *entry_text,
    const char *enabled_text)
{
    const std::filesystem::path path =
        path_text == nullptr
            ? std::filesystem::path{}
            : std::filesystem::path(path_text);

    std::size_t entry = 0U;
    const std::string_view entry_value =
        entry_text == nullptr
            ? std::string_view{}
            : std::string_view(entry_text);
    if (!parse_index(entry_value, entry)) {
        std::fprintf(stderr, "Invalid APT source entry identity.\n");
        return 2;
    }

    bool enabled = false;
    if (enabled_text != nullptr &&
        std::strcmp(enabled_text, "yes") == 0) {
        enabled = true;
    } else if (enabled_text == nullptr ||
               std::strcmp(enabled_text, "no") != 0) {
        std::fprintf(stderr, "APT source state must be yes or no.\n");
        return 2;
    }

    struct stat metadata {};
    if (!safe_existing_apt_source(path, metadata)) {
        std::fprintf(stderr, "Unsafe or unsupported APT source path.\n");
        return 2;
    }

    std::string content;
    if (!read_text(path, content)) {
        std::fprintf(
            stderr,
            "Unable to read APT source file %s.\n",
            path.c_str());
        return 3;
    }

    std::string updated;
    std::string error;
    bool changed = false;
    if (path.extension() == ".sources") {
        changed =
            infiltrator::software::set_apt_deb822_entry_enabled(
                content,
                entry,
                enabled,
                updated,
                error);
    } else {
        changed =
            infiltrator::software::set_apt_list_entry_enabled(
                content,
                entry,
                enabled,
                updated,
                error);
    }

    if (!changed) {
        std::fprintf(
            stderr,
            "Unable to change APT source: %s\n",
            error.c_str());
        return 4;
    }
    if (updated == content) {
        return 0;
    }

    if (!write_atomic(path, updated, &metadata)) {
        std::fprintf(
            stderr,
            "Unable to update %s: %s\n",
            path.c_str(),
            std::strerror(errno));
        return 5;
    }
    return 0;
}

int add_apt_source(
    const char *name,
    const char *uri,
    const char *suite,
    const char *components,
    const char *signed_by)
{
    const std::string name_text =
        name == nullptr ? std::string{} : std::string{name};
    const std::string uri_text =
        uri == nullptr ? std::string{} : std::string{uri};
    const std::string suite_text =
        suite == nullptr ? std::string{} : std::string{suite};
    const std::string components_text =
        components == nullptr ? std::string{} : std::string{components};
    const std::string signed_by_text =
        signed_by == nullptr ? std::string{} : std::string{signed_by};

    if (!safe_name(name_text) ||
        !https_uri(uri_text) ||
        !safe_token_list(suite_text) ||
        !safe_token_list(components_text) ||
        !valid_signed_by(signed_by_text)) {
        std::fprintf(stderr, "Invalid software-source parameters.\n");
        return 2;
    }

    const std::filesystem::path destination =
        std::filesystem::path("/etc/apt/sources.list.d") /
        ("infiltrator-software-" +
         normalise_name(name_text) + ".sources");

    std::string content;
    content += "Types: deb\n";
    content += "URIs: " + uri_text + "\n";
    content += "Suites: " + suite_text + "\n";
    content += "Components: " + components_text + "\n";
    if (!signed_by_text.empty()) {
        content += "Signed-By: " + signed_by_text + "\n";
    }
    content += "Enabled: yes\n";

    if (!write_atomic(destination, content)) {
        std::fprintf(
            stderr,
            "Unable to write %s: %s\n",
            destination.c_str(),
            std::strerror(errno));
        return 3;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (geteuid() != 0) {
        std::fprintf(
            stderr,
            "infiltrator-software-helper must run as root.\n");
        return 1;
    }

    if (argc == 7 &&
        std::strcmp(argv[1], "add-apt-source") == 0) {
        return add_apt_source(
            argv[2], argv[3], argv[4], argv[5], argv[6]);
    }

    if (argc == 5 &&
        std::strcmp(argv[1], "set-apt-source-enabled") == 0) {
        return set_apt_source_enabled(
            argv[2], argv[3], argv[4]);
    }

    std::fprintf(
        stderr,
        "Usage: infiltrator-software-helper "
        "add-apt-source NAME HTTPS_URI SUITE COMPONENTS SIGNED_BY\n"
        "   or: infiltrator-software-helper "
        "set-apt-source-enabled FILE ENTRY_INDEX yes|no\n");
    return 64;
}
