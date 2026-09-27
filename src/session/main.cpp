// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_policy.hpp"
#include "external/external_updates.hpp"

#include <glib.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using namespace infiltrator::software;

std::filesystem::path session_stamp_path(
    const char *name)
{
    const char *state = g_get_user_state_dir();
    if (state == nullptr || *state == '\0' ||
        name == nullptr || *name == '\0') {
        return {};
    }
    return std::filesystem::path(state) /
        "infiltrator-software" /
        name;
}

std::filesystem::path success_stamp_path()
{
    return session_stamp_path(
        "session-updates.last");
}

std::filesystem::path attempt_stamp_path()
{
    return session_stamp_path(
        "session-updates.attempt");
}

std::int64_t last_attempt_memory = 0;

std::int64_t now_unix()
{
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now()
                .time_since_epoch())
            .count());
}

std::int64_t read_stamp(
    const std::filesystem::path &path)
{
    std::ifstream input(path);
    std::int64_t value = 0;
    if (input) {
        input >> value;
    }
    return value;
}

bool write_stamp(
    const std::filesystem::path &path,
    const std::int64_t value)
{
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(
        path.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::string pattern =
        (path.parent_path() /
         (".session-stamp-" +
          std::to_string(
              static_cast<unsigned long long>(getpid())) +
          "-XXXXXX")).string();
    std::vector<char> writable(
        pattern.begin(),
        pattern.end());
    writable.push_back('\0');

    const int fd = mkstemp(writable.data());
    if (fd < 0) {
        return false;
    }

    const std::string text =
        std::to_string(value) + "\n";
    std::size_t offset = 0U;
    bool ok = true;
    while (offset < text.size()) {
        const ssize_t written =
            write(
                fd,
                text.data() + offset,
                text.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
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
    if (close(fd) != 0) {
        ok = false;
    }

    const std::filesystem::path temporary(
        writable.data());
    if (!ok ||
        rename(
            temporary.c_str(),
            path.c_str()) != 0) {
        std::filesystem::remove(temporary, ec);
        return false;
    }

    const int directory_fd =
        open(
            path.parent_path().c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) {
        return false;
    }
    const bool synced =
        fsync(directory_fd) == 0;
    const bool closed =
        close(directory_fd) == 0;
    return synced && closed;
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

    last_attempt_memory = now_unix();
    if (!write_stamp(
            attempt_stamp_path(),
            last_attempt_memory)) {
        g_warning(
            "Unable to persist the automatic session-update attempt timestamp; "
            "in-process retry backoff remains active.");
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

    if (success &&
        !write_stamp(
            success_stamp_path(),
            now_unix())) {
        g_warning(
            "Automatic session updates completed, but the success timestamp "
            "could not be durably persisted.");
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

    const std::int64_t last =
        std::max(
            {read_stamp(success_stamp_path()),
             read_stamp(attempt_stamp_path()),
             last_attempt_memory});
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
