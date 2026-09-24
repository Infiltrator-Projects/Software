// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/model.hpp"

#include <string>
#include <string_view>

namespace infiltrator::software {
namespace {

std::string package_base(const PackageRecord &package)
{
    std::string value =
        package.package_name.empty()
            ? package.id
            : package.package_name;
    const std::size_t colon = value.find(':');
    if (colon != std::string::npos) {
        value.erase(colon);
    }
    return value;
}

bool starts_with(
    const std::string_view value,
    const std::string_view prefix) noexcept
{
    return value.size() >= prefix.size() &&
           value.substr(0U, prefix.size()) == prefix;
}

bool ends_with(
    const std::string_view value,
    const std::string_view suffix) noexcept
{
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

} // namespace

std::string_view channel_name(const Channel channel) noexcept
{
    switch (channel) {
    case Channel::stable: return "Stable";
    case Channel::beta: return "Beta";
    case Channel::alpha: return "Alpha";
    case Channel::unknown: return "Unknown";
    }
    return "Unknown";
}

std::string_view package_kind_name(const PackageKind kind) noexcept
{
    switch (kind) {
    case PackageKind::application: return "Application";
    case PackageKind::system: return "System";
    case PackageKind::library: return "Library";
    case PackageKind::driver: return "Driver";
    case PackageKind::kernel: return "Kernel";
    case PackageKind::runtime: return "Runtime";
    case PackageKind::unknown: return "Unknown";
    }
    return "Unknown";
}

std::string_view transaction_action_name(const TransactionAction action) noexcept
{
    switch (action) {
    case TransactionAction::install: return "Install";
    case TransactionAction::upgrade: return "Upgrade";
    case TransactionAction::remove: return "Remove";
    }
    return "Unknown";
}

bool valid_identity(const PackageRecord &package) noexcept
{
    return !package.id.empty() && !package.name.empty();
}

void classify_package_role(PackageRecord &package) noexcept
{
    const std::string name = package_base(package);

    const bool kernel =
        starts_with(name, "linux-image") ||
        starts_with(name, "linux-modules") ||
        starts_with(name, "linux-headers") ||
        name == "linux-base";
    const bool driver =
        name == "linux-firmware" ||
        starts_with(name, "firmware-") ||
        starts_with(name, "nvidia-driver") ||
        starts_with(name, "xserver-xorg-video-") ||
        starts_with(name, "xserver-xorg-input-") ||
        ends_with(name, "-dkms");
    const bool core =
        package.essential ||
        package.priority == "required" ||
        package.priority == "important" ||
        name == "apt" ||
        name == "dpkg" ||
        name == "systemd" ||
        name == "libc6" ||
        name == "base-files" ||
        name == "init-system-helpers" ||
        starts_with(name, "grub-");

    package.system_critical =
        package.system_critical ||
        package.essential ||
        package.priority == "required" ||
        name == "dpkg" ||
        name == "systemd" ||
        name == "libc6" ||
        name == "linux-base" ||
        starts_with(name, "linux-image") ||
        starts_with(name, "linux-modules");

    if (kernel) {
        package.kind = PackageKind::kernel;
    } else if (driver) {
        package.kind = PackageKind::driver;
    } else if (core || package.system_critical) {
        package.kind = PackageKind::system;
    } else if (starts_with(name, "lib")) {
        package.kind = PackageKind::library;
    } else if (package.kind == PackageKind::unknown) {
        package.kind = PackageKind::application;
    }
}

bool is_system_component(const PackageRecord &package) noexcept
{
    return package.system_critical ||
           package.kind == PackageKind::system ||
           package.kind == PackageKind::driver ||
           package.kind == PackageKind::kernel;
}

} // namespace infiltrator::software
