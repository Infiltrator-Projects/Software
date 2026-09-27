// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_policy.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace infiltrator::software {
namespace {

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

bool parse_bool(const std::string_view value, const bool fallback)
{
    std::string lower;
    lower.reserve(value.size());
    for (const char raw : value) {
        const unsigned char ch =
            static_cast<unsigned char>(raw);
        lower.push_back(
            static_cast<char>(std::tolower(ch)));
    }
    if (lower == "1" || lower == "true" ||
        lower == "yes" || lower == "on") {
        return true;
    }
    if (lower == "0" || lower == "false" ||
        lower == "no" || lower == "off") {
        return false;
    }
    return fallback;
}

unsigned parse_unsigned(
    const std::string_view value,
    const unsigned fallback,
    const unsigned maximum = 10080U)
{
    unsigned parsed = 0U;
    const auto result =
        std::from_chars(
            value.data(),
            value.data() + value.size(),
            parsed);
    if (result.ec != std::errc{} ||
        result.ptr != value.data() + value.size()) {
        return fallback;
    }
    return std::min(parsed, maximum);
}

std::string bool_text(const bool value)
{
    return value ? "true" : "false";
}

bool path_locked(const char *path) noexcept
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    struct flock lock {};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    const int status = fcntl(fd, F_GETLK, &lock);
    close(fd);
    return status == 0 && lock.l_type != F_UNLCK;
}

std::string read_first_line(const std::filesystem::path &path)
{
    std::ifstream input(path);
    std::string value;
    if (input) {
        std::getline(input, value);
    }
    return trim(value);
}

} // namespace

std::string software_preferences_path()
{
    const char *override_path =
        std::getenv("INFILTRATOR_SOFTWARE_PREFERENCES_PATH");
    if (override_path != nullptr && *override_path != '\0') {
        return override_path;
    }
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return (
            std::filesystem::path(xdg) /
            "infiltrator-software" /
            "preferences.conf").string();
    }
    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return (
            std::filesystem::path(home) /
            ".config" /
            "infiltrator-software" /
            "preferences.conf").string();
    }
    return {};
}

bool load_software_preferences(
    SoftwarePreferences &preferences,
    std::string &error)
{
    preferences = SoftwarePreferences{};
    error.clear();
    const std::string path = software_preferences_path();
    if (path.empty()) {
        error = "Unable to determine Software preferences path.";
        return false;
    }
    std::ifstream input(path);
    if (!input) {
        if (!std::filesystem::exists(path)) {
            return true;
        }
        error = "Unable to read Software preferences.";
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::string clean = trim(line);
        if (clean.empty() || clean.front() == '#') continue;
        const std::size_t equals = clean.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(clean.substr(0U, equals));
        const std::string value = trim(clean.substr(equals + 1U));

        if (key == "refresh-schedule-enabled") {
            preferences.refresh_schedule_enabled =
                parse_bool(value, preferences.refresh_schedule_enabled);
        } else if (key == "first-refresh-minutes") {
            preferences.first_refresh_minutes =
                parse_unsigned(value, preferences.first_refresh_minutes);
        } else if (key == "recurring-refresh-minutes") {
            preferences.recurring_refresh_minutes =
                parse_unsigned(value, preferences.recurring_refresh_minutes);
        } else if (key == "notifications-enabled") {
            preferences.notifications_enabled =
                parse_bool(value, preferences.notifications_enabled);
        } else if (key == "notifications-security-only") {
            preferences.notifications_security_only =
                parse_bool(value, preferences.notifications_security_only);
        } else if (key == "notify-max-days") {
            preferences.notify_max_days =
                parse_unsigned(value, preferences.notify_max_days, 3650U);
        } else if (key == "notify-max-age-days") {
            preferences.notify_max_age_days =
                parse_unsigned(value, preferences.notify_max_age_days, 3650U);
        } else if (key == "notify-grace-period-days") {
            preferences.notify_grace_period_days =
                parse_unsigned(value, preferences.notify_grace_period_days, 3650U);
        } else if (key == "notify-days-between") {
            preferences.notify_days_between =
                parse_unsigned(value, preferences.notify_days_between, 3650U);
        } else if (key == "show-flatpak-updates") {
            preferences.show_flatpak_updates =
                parse_bool(value, preferences.show_flatpak_updates);
        } else if (key == "show-cinnamon-updates") {
            preferences.show_cinnamon_updates =
                parse_bool(value, preferences.show_cinnamon_updates);
        } else if (key == "auto-update-packages") {
            preferences.auto_update_packages =
                parse_bool(value, preferences.auto_update_packages);
        } else if (key == "auto-update-flatpaks") {
            preferences.auto_update_flatpaks =
                parse_bool(value, preferences.auto_update_flatpaks);
        } else if (key == "auto-update-cinnamon-spices") {
            preferences.auto_update_cinnamon_spices =
                parse_bool(value, preferences.auto_update_cinnamon_spices);
        } else if (key == "auto-remove-obsolete") {
            preferences.auto_remove_obsolete =
                parse_bool(value, preferences.auto_remove_obsolete);
        } else if (key == "hide-window-after-update") {
            preferences.hide_window_after_update =
                parse_bool(value, preferences.hide_window_after_update);
        } else if (key == "hide-tray") {
            preferences.hide_tray =
                parse_bool(value, preferences.hide_tray);
        } else if (key == "install-recommends") {
            preferences.install_recommends =
                parse_bool(value, preferences.install_recommends);
        } else if (key == "keep-configuration") {
            preferences.keep_configuration =
                parse_bool(value, preferences.keep_configuration);
        } else if (key == "snapshot-before-system-updates") {
            preferences.snapshot_before_system_updates =
                parse_bool(value, preferences.snapshot_before_system_updates);
        } else if (key == "ignore" && !value.empty()) {
            preferences.ignored_packages.push_back(value);
        }
    }
    return true;
}

bool save_software_preferences(
    const SoftwarePreferences &preferences,
    std::string &error)
{
    error.clear();
    const std::string path = software_preferences_path();
    if (path.empty()) {
        error = "Unable to determine Software preferences path.";
        return false;
    }

    const std::filesystem::path target(path);
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        error = "Unable to create Software preferences directory: " +
                ec.message();
        return false;
    }

    const std::filesystem::path temporary =
        target.string() + ".tmp." +
        std::to_string(
            static_cast<unsigned long long>(getpid()));
    std::ofstream output(temporary, std::ios::out | std::ios::trunc);
    if (!output) {
        error = "Unable to write Software preferences.";
        return false;
    }

    output
        << "refresh-schedule-enabled=" << bool_text(preferences.refresh_schedule_enabled) << '\n'
        << "first-refresh-minutes=" << preferences.first_refresh_minutes << '\n'
        << "recurring-refresh-minutes=" << preferences.recurring_refresh_minutes << '\n'
        << "notifications-enabled=" << bool_text(preferences.notifications_enabled) << '\n'
        << "notifications-security-only=" << bool_text(preferences.notifications_security_only) << '\n'
        << "notify-max-days=" << preferences.notify_max_days << '\n'
        << "notify-max-age-days=" << preferences.notify_max_age_days << '\n'
        << "notify-grace-period-days=" << preferences.notify_grace_period_days << '\n'
        << "notify-days-between=" << preferences.notify_days_between << '\n'
        << "show-flatpak-updates=" << bool_text(preferences.show_flatpak_updates) << '\n'
        << "show-cinnamon-updates=" << bool_text(preferences.show_cinnamon_updates) << '\n'
        << "auto-update-packages=" << bool_text(preferences.auto_update_packages) << '\n'
        << "auto-update-flatpaks=" << bool_text(preferences.auto_update_flatpaks) << '\n'
        << "auto-update-cinnamon-spices=" << bool_text(preferences.auto_update_cinnamon_spices) << '\n'
        << "auto-remove-obsolete=" << bool_text(preferences.auto_remove_obsolete) << '\n'
        << "hide-window-after-update=" << bool_text(preferences.hide_window_after_update) << '\n'
        << "hide-tray=" << bool_text(preferences.hide_tray) << '\n'
        << "install-recommends=" << bool_text(preferences.install_recommends) << '\n'
        << "keep-configuration=" << bool_text(preferences.keep_configuration) << '\n'
        << "snapshot-before-system-updates=" << bool_text(preferences.snapshot_before_system_updates) << '\n';
    for (const std::string &rule : preferences.ignored_packages) {
        if (!rule.empty() &&
            rule.find('\n') == std::string::npos &&
            rule.find('\r') == std::string::npos) {
            output << "ignore=" << rule << '\n';
        }
    }
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, ec);
        error = "Unable to finish writing Software preferences.";
        return false;
    }

    if (chmod(temporary.c_str(), 0600) != 0) {
        std::filesystem::remove(temporary, ec);
        error = "Unable to secure Software preferences before publication.";
        return false;
    }

    const int temporary_fd =
        open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
    if (temporary_fd < 0) {
        std::filesystem::remove(temporary, ec);
        error = "Unable to reopen Software preferences for durability verification.";
        return false;
    }
    const bool file_synced =
        fsync(temporary_fd) == 0;
    const bool file_closed =
        close(temporary_fd) == 0;
    if (!file_synced || !file_closed) {
        std::filesystem::remove(temporary, ec);
        error = "Unable to durably write Software preferences.";
        return false;
    }

    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        error = "Unable to publish Software preferences atomically.";
        return false;
    }

    const int directory_fd =
        open(
            target.parent_path().c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) {
        error = "Unable to open Software preferences directory for durability verification.";
        return false;
    }
    const bool directory_synced =
        fsync(directory_fd) == 0;
    const bool directory_closed =
        close(directory_fd) == 0;
    if (!directory_synced || !directory_closed) {
        error = "Unable to durably publish Software preferences.";
        return false;
    }
    return true;
}

bool wildcard_match(
    const std::string_view pattern,
    const std::string_view value) noexcept
{
    std::size_t p = 0U;
    std::size_t v = 0U;
    std::size_t star = std::string_view::npos;
    std::size_t retry = 0U;
    while (v < value.size()) {
        if (p < pattern.size() &&
            (pattern[p] == '?' || pattern[p] == value[v])) {
            ++p;
            ++v;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = v;
        } else if (star != std::string_view::npos) {
            p = star + 1U;
            v = ++retry;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool update_is_ignored(
    const PackageRecord &package,
    const SoftwarePreferences &preferences) noexcept
{
    const std::string_view identity =
        package.source_package.empty()
            ? (package.package_name.empty()
                   ? std::string_view(package.id)
                   : std::string_view(package.package_name))
            : std::string_view(package.source_package);
    for (const std::string &rule : preferences.ignored_packages) {
        const std::size_t equals = rule.find('=');
        const std::string_view pattern =
            equals == std::string::npos
                ? std::string_view(rule)
                : std::string_view(rule).substr(0U, equals);
        const std::string_view version =
            equals == std::string::npos
                ? std::string_view{}
                : std::string_view(rule).substr(equals + 1U);
        if (wildcard_match(pattern, identity) &&
            (version.empty() ||
             version == package.available_version)) {
            return true;
        }
    }
    return false;
}

bool reboot_required(const std::string &marker_path)
{
    std::error_code ec;
    return std::filesystem::exists(marker_path, ec) && !ec;
}

std::vector<std::string> reboot_required_packages(
    const std::string &list_path)
{
    std::ifstream input(list_path);
    std::vector<std::string> result;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.empty()) result.push_back(std::move(line));
    }
    return result;
}

bool package_manager_locked() noexcept
{
    return path_locked("/var/lib/dpkg/lock-frontend") ||
           path_locked("/var/lib/dpkg/lock") ||
           path_locked("/var/lib/apt/lists/lock") ||
           path_locked("/var/cache/apt/archives/lock");
}

bool system_on_battery() noexcept
{
    const std::filesystem::path root("/sys/class/power_supply");
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) return false;

    bool battery_present = false;
    bool mains_present = false;
    bool mains_online = false;
    for (const auto &entry :
         std::filesystem::directory_iterator(
             root,
             std::filesystem::directory_options::skip_permission_denied,
             ec)) {
        if (ec) break;
        const std::string type =
            read_first_line(entry.path() / "type");
        if (type == "Battery") {
            const std::string present =
                read_first_line(entry.path() / "present");
            if (present.empty() || present == "1") {
                battery_present = true;
            }
        } else if (
            type == "Mains" || type == "AC" ||
            type == "USB" || type == "USB_C") {
            mains_present = true;
            if (read_first_line(entry.path() / "online") == "1") {
                mains_online = true;
            }
        }
    }
    return battery_present && mains_present && !mains_online;
}

} // namespace infiltrator::software
