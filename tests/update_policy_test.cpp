// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_policy.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

int main()
{
    using namespace infiltrator::software;

    assert(wildcard_match("linux-*", "linux-image-generic"));
    assert(wildcard_match("fire?ox", "firefox"));
    assert(!wildcard_match("linux-*", "firefox"));

    SoftwarePreferences preferences;
    preferences.ignored_packages = {"firefox", "linux-*=6.8.0-99"};
    PackageRecord firefox;
    firefox.source_package = "firefox";
    firefox.available_version = "1";
    assert(update_is_ignored(firefox, preferences));

    PackageRecord kernel;
    kernel.source_package = "linux-meta";
    kernel.available_version = "6.8.0-99";
    assert(update_is_ignored(kernel, preferences));
    kernel.available_version = "6.8.0-100";
    assert(!update_is_ignored(kernel, preferences));

    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        ("software-preferences-" +
         std::to_string(
             static_cast<unsigned long long>(getpid())) +
         ".conf");
    setenv(
        "INFILTRATOR_SOFTWARE_PREFERENCES_PATH",
        temporary.c_str(),
        1);

    SoftwarePreferences saved;
    saved.auto_update_packages = true;
    saved.recurring_refresh_minutes = 90U;
    saved.hide_tray = true;
    saved.ignored_packages = {"foo*", "bar=2"};
    std::string error;
    assert(save_software_preferences(saved, error));

    SoftwarePreferences loaded;
    assert(load_software_preferences(loaded, error));
    assert(loaded.auto_update_packages);
    assert(loaded.recurring_refresh_minutes == 90U);
    assert(loaded.hide_tray);
    assert(loaded.ignored_packages.size() == 2U);

    const std::filesystem::path reboot =
        temporary.string() + ".reboot";
    {
        std::ofstream marker(reboot);
        marker << "reboot";
    }
    assert(reboot_required(reboot.string()));
    std::filesystem::remove(reboot);
    assert(!reboot_required(reboot.string()));

    std::filesystem::remove(temporary);
    unsetenv("INFILTRATOR_SOFTWARE_PREFERENCES_PATH");
    return 0;
}
