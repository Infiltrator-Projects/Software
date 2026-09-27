// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_policy.hpp"
#include "external/external_updates.hpp"

#include <glib.h>

#include <string>

int main()
{
    using namespace infiltrator::software;

    SoftwarePreferences preferences;
    std::string error;
    if (!load_software_preferences(
            preferences, error)) {
        g_printerr(
            "Unable to load Software preferences: %s\n",
            error.c_str());
        return 1;
    }

    bool success = true;

    if (preferences.auto_update_flatpaks) {
        std::string flatpak_error;
        if (!apply_flatpak_updates(
                true,
                true,
                flatpak_error)) {
            g_printerr(
                "Automatic Flatpak update failed: %s\n",
                flatpak_error.c_str());
            success = false;
        }
    }

    if (preferences.auto_update_cinnamon_spices) {
        std::string cinnamon_error;
        if (!apply_cinnamon_updates(
                cinnamon_error)) {
            g_printerr(
                "Automatic Cinnamon Spice update failed: %s\n",
                cinnamon_error.c_str());
            success = false;
        }
    }

    return success ? 0 : 1;
}
