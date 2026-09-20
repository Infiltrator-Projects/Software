// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_installed_state.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace infiltrator::software {
namespace {

constexpr const char *kDpkgStatusPath = "/var/lib/dpkg/status";

std::string trim(const std::string_view value)
{
    std::size_t first = 0U;
    while (first < value.size() &&
           std::isspace(
               static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first &&
           std::isspace(
               static_cast<unsigned char>(value[last - 1U])) != 0) {
        --last;
    }

    return std::string(value.substr(first, last - first));
}

std::string field_key(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::uint64_t kib_to_bytes(const std::string_view text)
{
    std::uint64_t kib = 0U;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), kib);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        return 0U;
    }

    constexpr std::uint64_t kibibyte = 1024U;
    if (kib > std::numeric_limits<std::uint64_t>::max() / kibibyte) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return kib * kibibyte;
}

bool installed_status(const std::string_view value)
{
    std::istringstream words{std::string(value)};
    std::string want;
    std::string error;
    std::string state;
    words >> want >> error >> state;
    return !want.empty() && error == "ok" && state == "installed";
}

using Fields = std::map<std::string, std::string>;

void append_package(
    const Fields &fields,
    std::vector<PackageRecord> &packages)
{
    const auto status = fields.find("status");
    const auto name = fields.find("package");
    const auto version = fields.find("version");

    if (status == fields.end() ||
        name == fields.end() ||
        version == fields.end() ||
        name->second.empty() ||
        version->second.empty() ||
        !installed_status(status->second)) {
        return;
    }

    PackageRecord package;
    package.id = name->second;

    const auto architecture = fields.find("architecture");
    if (architecture != fields.end()) {
        package.architecture = architecture->second;
    }

    const auto multi_arch = fields.find("multi-arch");
    if (multi_arch != fields.end() &&
        multi_arch->second == "same" &&
        !package.architecture.empty()) {
        package.id += ":" + package.architecture;
    }

    package.name = package.id;
    package.package_name = package.id;
    package.installed_version = version->second;
    package.available_version = package.installed_version;
    package.source = "Debian";
    package.state = InstallState::installed;

    const auto size = fields.find("installed-size");
    if (size != fields.end()) {
        package.installed_size_bytes = kib_to_bytes(size->second);
    }

    if (valid_identity(package)) {
        packages.emplace_back(std::move(package));
    }
}

} // namespace

bool DebianInstalledState::available() noexcept
{
    return access(kDpkgStatusPath, R_OK) == 0;
}

std::vector<PackageRecord> DebianInstalledState::read(
    std::string &error)
{
    error.clear();

    std::ifstream input(kDpkgStatusPath, std::ios::binary);
    if (!input) {
        error =
            "Unable to read the installed Debian package database at "
            "/var/lib/dpkg/status.";
        return {};
    }

    std::ostringstream content;
    content << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error =
            "Unable to finish reading the installed Debian package database.";
        return {};
    }

    return parse(content.str(), error);
}

std::vector<PackageRecord> DebianInstalledState::parse(
    const std::string_view content,
    std::string &error)
{
    error.clear();
    std::vector<PackageRecord> packages;
    Fields fields;
    std::string current_key;

    auto flush = [&]() {
        if (!fields.empty()) {
            append_package(fields, packages);
        }
        fields.clear();
        current_key.clear();
    };

    std::size_t start = 0U;
    while (start <= content.size()) {
        const std::size_t newline = content.find('\n', start);
        const std::size_t end =
            newline == std::string_view::npos ? content.size() : newline;

        std::string_view line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }

        if (line.find_first_not_of(" \t") == std::string_view::npos) {
            flush();
        } else if (
            (line.front() == ' ' || line.front() == '\t') &&
            !current_key.empty()) {
            fields[current_key] += "\n";
            fields[current_key] += trim(line);
        } else {
            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                error = "Malformed Debian control line without a field separator.";
                return {};
            } else {
                current_key =
                    field_key(trim(line.substr(0U, colon)));
                if (current_key.empty()) {
                    error = "Debian control field name is empty.";
                    return {};
                }
                const auto inserted = fields.emplace(
                    current_key, trim(line.substr(colon + 1U)));
                if (!inserted.second) {
                    error =
                        "Duplicate Debian control field: " +
                        current_key;
                    return {};
                }
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1U;
    }

    flush();

    std::sort(
        packages.begin(), packages.end(),
        [](const PackageRecord &left, const PackageRecord &right) {
            return left.name < right.name;
        });

    return packages;
}

} // namespace infiltrator::software
