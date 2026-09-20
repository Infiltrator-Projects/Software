// SPDX-License-Identifier: GPL-3.0-or-later
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

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
            ch == '/' || ch == ':' || std::isspace(ch) != 0) {
            continue;
        }
        return false;
    }
    return true;
}

bool https_uri(std::string_view value)
{
    return value.rfind("https://", 0U) == 0U &&
           value.size() <= 2048U &&
           value.find('\n') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos;
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

bool write_atomic(
    const std::filesystem::path &path,
    const std::string &content)
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

    if (ok && fsync(fd) != 0) {
        ok = false;
    }
    if (fchmod(fd, 0644) != 0) {
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

    std::fprintf(
        stderr,
        "Usage: infiltrator-software-helper "
        "add-apt-source NAME HTTPS_URI SUITE COMPONENTS SIGNED_BY\n");
    return 64;
}
