// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_UPDATE_POLICY_HPP
#define INFILTRATOR_SOFTWARE_UPDATE_POLICY_HPP

#include "core/model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct SoftwarePreferences {
    bool refresh_schedule_enabled{true};
    unsigned first_refresh_minutes{10U};
    unsigned recurring_refresh_minutes{120U};
    bool notifications_enabled{true};
    bool notifications_security_only{true};
    unsigned notify_max_days{7U};
    unsigned notify_max_age_days{15U};
    unsigned notify_grace_period_days{30U};
    unsigned notify_days_between{2U};
    bool show_flatpak_updates{true};
    bool show_cinnamon_updates{true};
    bool auto_update_packages{false};
    bool auto_update_flatpaks{false};
    bool auto_update_cinnamon_spices{false};
    bool hide_window_after_update{false};
    bool hide_tray{false};
    bool install_recommends{false};
    bool keep_configuration{false};
    bool snapshot_before_system_updates{false};
    std::vector<std::string> ignored_packages;
};

std::string software_preferences_path();
bool load_software_preferences(
    SoftwarePreferences &preferences,
    std::string &error);
bool save_software_preferences(
    const SoftwarePreferences &preferences,
    std::string &error);

bool wildcard_match(
    std::string_view pattern,
    std::string_view value) noexcept;
bool update_is_ignored(
    const PackageRecord &package,
    const SoftwarePreferences &preferences) noexcept;

bool reboot_required(
    const std::string &marker_path = "/var/run/reboot-required");
std::vector<std::string> reboot_required_packages(
    const std::string &list_path =
        "/var/run/reboot-required.pkgs");
bool package_manager_locked() noexcept;
bool system_on_battery() noexcept;

} // namespace infiltrator::software

#endif
