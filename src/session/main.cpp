// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_policy.hpp"
#include "external/external_updates.hpp"

#include <glib.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace infiltrator::software;

std::filesystem::path session_stamp_path()
{
    const char *state = g_get_user_state_dir();
    if (state == nullptr || *state == '\0') {
        return {};
    }
    return std::filesystem::path(state) /
        "infiltrator-software" /
        "session-updates.last";
}

std::int64_t now_unix()
{
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now()
                .time_since_epoch())
            .count());
}

std::int64_t read_stamp()
{
    std::ifstream input(session_stamp_path());
    std::int64_t value = 0;
    if (input) {
        input >> value;
    }
    return value;
}

void write_stamp()
{
    const std::filesystem::path path =
        session_stamp_path();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(
        path.parent_path(), ec);
    if (ec) {
        return;
    }
    std::ofstream output(
        path,
        std::ios::out | std::ios::trunc);
    if (output) {
        output << now_unix() << '\n';
    }
}

bool run_updates()
{
    SoftwarePreferences preferences;
    std::string error;
    if (!load_software_preferences(
            preferences, error)) {
        g_warning(
            "Unable to load Software preferences: %s",
            error.c_str());
        return false;
    }

    if (!preferences.auto_update_flatpaks &&
        !preferences.auto_update_cinnamon_spices) {
        return true;
    }

    if (system_on_battery()) {
        g_message(
            "Automatic Software session updates deferred while on battery power.");
        return true;
    }

    bool success = true;
    if (preferences.auto_update_flatpaks) {
        std::string flatpak_error;
        if (!apply_flatpak_updates(
                true,
                true,
                flatpak_error)) {
            g_warning(
                "Automatic Flatpak update failed: %s",
                flatpak_error.c_str());
            success = false;
        }
    }

    if (preferences.auto_update_cinnamon_spices) {
        std::string cinnamon_error;
        if (!apply_cinnamon_updates(
                cinnamon_error)) {
            g_warning(
                "Automatic Cinnamon Spice update failed: %s",
                cinnamon_error.c_str());
            success = false;
        }
    }

    if (success) {
        write_stamp();
    }
    return success;
}

gboolean check_due(gpointer user_data)
{
    auto *started =
        static_cast<gint64 *>(user_data);

    SoftwarePreferences preferences;
    std::string error;
    if (!load_software_preferences(
            preferences, error)) {
        return G_SOURCE_CONTINUE;
    }

    if (!preferences.auto_update_flatpaks &&
        !preferences.auto_update_cinnamon_spices) {
        return G_SOURCE_CONTINUE;
    }

    const std::int64_t last = read_stamp();
    const std::int64_t now = now_unix();
    if (last <= 0) {
        const gint64 elapsed =
            g_get_monotonic_time() - *started;
        const gint64 first_delay =
            static_cast<gint64>(
                std::max(
                    preferences.first_refresh_minutes,
                    1U)) *
            60 * G_USEC_PER_SEC;
        if (elapsed < first_delay) {
            return G_SOURCE_CONTINUE;
        }
    } else {
        const std::int64_t interval =
            static_cast<std::int64_t>(
                std::max(
                    preferences.recurring_refresh_minutes,
                    1U)) *
            60;
        if (now - last < interval) {
            return G_SOURCE_CONTINUE;
        }
    }

    (void)run_updates();
    return G_SOURCE_CONTINUE;
}

} // namespace

int main()
{
    gint64 started = g_get_monotonic_time();
    GMainLoop *loop =
        g_main_loop_new(nullptr, FALSE);
    if (loop == nullptr) {
        return 1;
    }

    /*
     * Poll preferences once per minute. The configured first/recurring
     * interval is enforced by the due check, so preference changes take effect
     * without restarting the session updater and no busy polling is involved.
     */
    g_timeout_add_seconds(
        60U,
        check_due,
        &started);
    (void)check_due(&started);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
