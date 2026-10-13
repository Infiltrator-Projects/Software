// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_installed_state.hpp"

#include "engine/debian_control.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace infiltrator::software {
namespace {

constexpr const char *kDpkgStatusPath = "/var/lib/dpkg/status";

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

bool installed_status(
    const std::string_view value,
    bool &held)
{
    held = false;
    std::istringstream words{std::string(value)};
    std::string want;
    std::string error;
    std::string state;
    words >> want >> error >> state;
    if (want.empty() || error != "ok" || state != "installed") {
        return false;
    }
    held = want == "hold";
    return true;
}

using Fields = DebianControlFields;

void append_package(
    const Fields &fields,
    std::vector<PackageRecord> &packages)
{
    const auto status = fields.find("status");
    const auto name = fields.find("package");
    const auto version = fields.find("version");

    bool held = false;
    if (status == fields.end() ||
        name == fields.end() ||
        version == fields.end() ||
        name->second.empty() ||
        version->second.empty() ||
        !installed_status(status->second, held)) {
        return;
    }

    PackageRecord package;
    package.held = held;
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

    if (const auto depends = fields.find("depends"); depends != fields.end()) {
        package.depends = depends->second;
    }
    if (const auto pre_depends = fields.find("pre-depends"); pre_depends != fields.end()) {
        package.pre_depends = pre_depends->second;
    }
    if (const auto provides = fields.find("provides"); provides != fields.end()) {
        package.provides = provides->second;
    }
    if (const auto conflicts = fields.find("conflicts"); conflicts != fields.end()) {
        package.conflicts = conflicts->second;
    }
    if (const auto breaks = fields.find("breaks"); breaks != fields.end()) {
        package.breaks = breaks->second;
    }
    if (const auto replaces = fields.find("replaces"); replaces != fields.end()) {
        package.replaces = replaces->second;
    }
    if (const auto priority = fields.find("priority"); priority != fields.end()) {
        package.priority = priority->second;
    }
    if (multi_arch != fields.end()) {
        package.multi_arch = multi_arch->second;
    }
    if (const auto essential = fields.find("essential"); essential != fields.end()) {
        package.essential =
            essential->second == "yes" || essential->second == "true";
    }
    if (const auto protected_field = fields.find("protected");
        protected_field != fields.end()) {
        package.protected_package =
            protected_field->second == "yes" ||
            protected_field->second == "true";
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
    if (!parse_debian_control_records(
            content,
            [&](const DebianControlFields &fields) {
                append_package(fields, packages);
            },
            error)) {
        return {};
    }

    std::sort(
        packages.begin(), packages.end(),
        [](const PackageRecord &left, const PackageRecord &right) {
            return left.name < right.name;
        });

    return packages;
}

} // namespace infiltrator::software
