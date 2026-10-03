// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/engine_client.hpp"
#include "core/update_policy.hpp"
#include "core/update_tracker.hpp"
#include "core/transaction_history.hpp"
#include "external/external_updates.hpp"

#include <gtk/gtk.h>
#include <libxapp/xapp-status-icon.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using infiltrator::software::EngineClient;
using infiltrator::software::PackageRecord;
using infiltrator::software::SoftwarePreferences;
using infiltrator::software::load_software_preferences;
using infiltrator::software::software_preferences_path;
using infiltrator::software::update_is_ignored;
using infiltrator::software::UpdateNotificationResult;
using infiltrator::software::evaluate_update_notification;
using infiltrator::software::update_tracker_path;
using infiltrator::software::TransactionHistoryStore;
using infiltrator::software::ExternalUpdate;
using infiltrator::software::discover_flatpak_updates;
using infiltrator::software::discover_cinnamon_updates;

struct CheckResult {
    std::vector<PackageRecord> updates;
    std::size_t external_update_count{0U};
    std::string error;
};

struct CheckTaskData {
    bool refresh_metadata{false};
};

struct FileStamp {
    bool exists{false};
    std::uintmax_t size{0U};
    std::filesystem::file_time_type modified{};
};

bool operator==(const FileStamp &left, const FileStamp &right) noexcept
{
    return left.exists == right.exists &&
           left.size == right.size &&
           left.modified == right.modified;
}

struct TrayState {
    XAppStatusIcon *icon{};
    bool checking{false};
    std::size_t update_count{0U};
    std::string last_error;
    bool opening_software{false};
    guint opening_reset_id{0U};
    GDBusConnection *engine_connection{};
    guint state_signal_id{0U};
    guint health_signal_id{0U};
    SoftwarePreferences preferences{};
    std::string override_state;
    FileStamp preferences_stamp{};
    FileStamp override_stamp{};
    bool runtime_inputs_initialized{false};
    gint64 started_us{0};
    gint64 last_metadata_refresh_us{0};
    bool replacement_exec_failed{false};
    GFileMonitor *preferences_monitor{};
    GFileMonitor *override_monitor{};
    GFileMonitor *executable_monitor{};
};

constexpr const char *kInstalledTrayPath =
    "/usr/bin/infiltrator-software-tray";
constexpr std::string_view kDeletedSuffix = " (deleted)";

bool take_replace_argument(int &argc, char **argv)
{
    bool replace_existing = false;
    int write = 1;
    for (int read = 1; read < argc; ++read) {
        if (argv[read] != nullptr &&
            std::string_view(argv[read]) == "--replace") {
            replace_existing = true;
            continue;
        }
        argv[write++] = argv[read];
    }
    argc = write;
    argv[argc] = nullptr;
    return replace_existing;
}

bool tray_executable_name(std::string name)
{
    if (name.size() >= kDeletedSuffix.size() &&
        name.compare(
            name.size() - kDeletedSuffix.size(),
            kDeletedSuffix.size(),
            kDeletedSuffix) == 0) {
        name.resize(name.size() - kDeletedSuffix.size());
    }
    return name == "infiltrator-software-tray";
}

bool same_user_tray_process(const pid_t pid)
{
    if (pid <= 1 || pid == getpid()) {
        return false;
    }

    const std::filesystem::path process =
        std::filesystem::path("/proc") /
        std::to_string(static_cast<long long>(pid));

    struct stat process_stat {};
    if (::stat(process.c_str(), &process_stat) != 0 ||
        process_stat.st_uid != getuid()) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path executable =
        std::filesystem::read_symlink(
            process / "exe", ec);
    if (ec) {
        return false;
    }
    return tray_executable_name(
        executable.filename().string());
}

void stop_same_user_tray_processes()
{
    std::error_code ec;
    std::filesystem::directory_iterator iterator(
        "/proc",
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    const std::filesystem::directory_iterator end;
    for (; !ec && iterator != end; iterator.increment(ec)) {
        const std::string name =
            iterator->path().filename().string();
        pid_t pid = 0;
        const auto parsed =
            std::from_chars(
                name.data(),
                name.data() + name.size(),
                pid);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != name.data() + name.size() ||
            !same_user_tray_process(pid)) {
            continue;
        }

        if (::kill(pid, SIGTERM) != 0 && errno != ESRCH) {
            g_debug(
                "Unable to stop stale Software tray process %ld: %s",
                static_cast<long>(pid),
                g_strerror(errno));
        }
    }
}

bool installed_tray_replaced()
{
    std::error_code ec;
    std::filesystem::path running =
        std::filesystem::read_symlink(
            "/proc/self/exe", ec);
    if (ec) {
        return false;
    }

    std::string running_text = running.string();
    bool deleted = false;
    if (running_text.size() >= kDeletedSuffix.size() &&
        running_text.compare(
            running_text.size() - kDeletedSuffix.size(),
            kDeletedSuffix.size(),
            kDeletedSuffix) == 0) {
        running_text.resize(
            running_text.size() - kDeletedSuffix.size());
        deleted = true;
    }

    if (running_text != kInstalledTrayPath) {
        return false;
    }

    struct stat installed {};
    if (::stat(kInstalledTrayPath, &installed) != 0) {
        return false;
    }
    if (deleted) {
        return true;
    }

    struct stat running_stat {};
    if (::stat("/proc/self/exe", &running_stat) != 0) {
        return false;
    }
    return installed.st_dev != running_stat.st_dev ||
           installed.st_ino != running_stat.st_ino;
}

int acquire_single_instance_lock(const bool replace_existing)
{
    const char *runtime = g_get_user_runtime_dir();
    if (runtime == nullptr || *runtime == '\0') {
        return -1;
    }

    const std::filesystem::path directory =
        std::filesystem::path(runtime) / "infiltrator-software";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return -1;
    }

    const std::filesystem::path path = directory / "tray.lock";
    const int fd = open(
        path.c_str(),
        O_CREAT | O_RDWR | O_CLOEXEC,
        0600);
    if (fd < 0) {
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        return fd;
    }
    if (!replace_existing) {
        close(fd);
        return -2;
    }

    /*
     * The tray is a session-resident executable.  dpkg can replace the file
     * underneath a still-running process, so a newly installed GUI must be
     * able to evict that stale same-user instance.  Match /proc/<pid>/exe,
     * not a process name, so unrelated applications cannot be terminated.
     */
    stop_same_user_tray_processes();

    constexpr guint kReplacementAttempts = 100U;
    constexpr gulong kReplacementDelayUs = 50000U;
    for (guint attempt = 0U;
         attempt < kReplacementAttempts;
         ++attempt) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            return fd;
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            close(fd);
            return -1;
        }
        if (attempt + 1U < kReplacementAttempts) {
            g_usleep(kReplacementDelayUs);
        }
    }

    close(fd);
    return -2;
}

std::filesystem::path state_file()
{
    const char *runtime = g_get_user_runtime_dir();
    if (runtime == nullptr || *runtime == '\0') {
        return {};
    }
    return std::filesystem::path(runtime) /
           "infiltrator-software" / "update-state";
}

std::string read_override()
{
    const std::filesystem::path path = state_file();
    if (path.empty()) {
        return {};
    }

    std::ifstream input(path);
    if (!input) {
        return {};
    }

    std::ostringstream text;
    text << input.rdbuf();
    std::string value = text.str();
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

FileStamp file_stamp(const std::filesystem::path &path)
{
    FileStamp stamp;
    if (path.empty()) {
        return stamp;
    }
    std::error_code ec;
    stamp.exists = std::filesystem::exists(path, ec) && !ec;
    if (!stamp.exists) {
        return stamp;
    }
    if (std::filesystem::is_regular_file(path, ec) && !ec) {
        stamp.size = std::filesystem::file_size(path, ec);
        if (ec) stamp.size = 0U;
    }
    ec.clear();
    stamp.modified = std::filesystem::last_write_time(path, ec);
    if (ec) stamp.modified = {};
    return stamp;
}

bool refresh_runtime_inputs(TrayState *state, const bool force = false)
{
    if (state == nullptr) {
        return false;
    }

    bool changed = false;
    const std::filesystem::path preferences_path =
        software_preferences_path();
    const FileStamp preferences_now = file_stamp(preferences_path);
    if (force || !state->runtime_inputs_initialized ||
        !(preferences_now == state->preferences_stamp)) {
        SoftwarePreferences preferences;
        std::string error;
        if (load_software_preferences(preferences, error)) {
            state->preferences = std::move(preferences);
        } else if (!error.empty()) {
            g_debug("Unable to reload Software preferences: %s", error.c_str());
        }
        state->preferences_stamp = preferences_now;
        changed = true;
    }

    const std::filesystem::path override_path = state_file();
    const FileStamp override_now = file_stamp(override_path);
    if (force || !state->runtime_inputs_initialized ||
        !(override_now == state->override_stamp)) {
        state->override_state = read_override();
        state->override_stamp = override_now;
        changed = true;
    }

    state->runtime_inputs_initialized = true;
    return changed;
}

std::int64_t last_successful_update()
{
    std::int64_t latest = 0;
    const auto consume =
        [&](const std::string &path) {
            if (path.empty() ||
                !std::filesystem::exists(path)) {
                return;
            }
            TransactionHistoryStore store(path);
            std::string error;
            const auto items =
                store.load_recent(100U, error);
            if (!error.empty()) {
                return;
            }
            for (const auto &item : items) {
                if (item.success) {
                    latest =
                        std::max(
                            latest,
                            item.completed_at_unix);
                }
            }
        };

    consume(
        infiltrator::software::user_transaction_history_path());
    consume(
        infiltrator::software::system_transaction_history_path());
    return latest;
}

void send_update_notification(
    const std::size_t count,
    const std::size_t relevant_count,
    const unsigned oldest_days)
{
    GError *error = nullptr;
    GDBusConnection *connection =
        g_bus_get_sync(
            G_BUS_TYPE_SESSION,
            nullptr,
            &error);
    if (connection == nullptr) {
        g_clear_error(&error);
        return;
    }

    const std::string summary =
        count == 1U
            ? "Software update available"
            : std::to_string(count) +
                  " software updates available";
    std::string body;
    if (relevant_count > 0U) {
        body =
            std::to_string(relevant_count) +
            (relevant_count == 1U
                 ? " security/kernel update has"
                 : " security/kernel updates have") +
            " remained outstanding";
        if (oldest_days > 0U) {
            body +=
                " for up to " +
                std::to_string(oldest_days) +
                (oldest_days == 1U
                     ? " day."
                     : " days.");
        } else {
            body += ".";
        }
    } else {
        body =
            "Open Software to review and install the available updates.";
    }

    const gchar *actions[] = {
        nullptr
    };
    GVariantBuilder hints;
    g_variant_builder_init(
        &hints,
        G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(
        &hints,
        "{sv}",
        "desktop-entry",
        g_variant_new_string(
            "net.ssmith.infiltrator.software"));

    g_dbus_connection_call(
        connection,
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        "Notify",
        g_variant_new(
            "(susss^as@a{sv}i)",
            "Software",
            0U,
            "infiltrator-software-updates-available-symbolic",
            summary.c_str(),
            body.c_str(),
            actions,
            g_variant_builder_end(&hints),
            -1),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        nullptr,
        nullptr);
    g_object_unref(connection);
}

void evaluate_notification(
    TrayState *state,
    const std::vector<PackageRecord> &updates,
    const std::size_t total_count)
{
    if (state == nullptr ||
        !state->last_error.empty()) {
        return;
    }

    UpdateNotificationResult result;
    std::string error;
    if (!evaluate_update_notification(
            updates,
            state->preferences,
            static_cast<std::int64_t>(
                std::time(nullptr)),
            last_successful_update(),
            update_tracker_path(),
            result,
            error)) {
        if (!error.empty()) {
            g_debug(
                "Unable to evaluate Software update notification: %s",
                error.c_str());
        }
        return;
    }

    if (result.notify) {
        send_update_notification(
            total_count,
            result.relevant_updates,
            result.oldest_age_days);
    }
}

void render(TrayState *state)
{
    if (state == nullptr || state->icon == nullptr) {
        return;
    }

    const std::string &override = state->override_state;
    if (override == "installing") {
        xapp_status_icon_set_icon_name(
            state->icon, "infiltrator-software-installing-symbolic");
        xapp_status_icon_set_tooltip_text(
            state->icon, "Installing software updates");
        xapp_status_icon_set_visible(state->icon, TRUE);
        return;
    }
    if (override == "checking") {
        xapp_status_icon_set_icon_name(
            state->icon, "infiltrator-software-checking-symbolic");
        xapp_status_icon_set_tooltip_text(
            state->icon, "Checking for software updates");
        xapp_status_icon_set_visible(state->icon, TRUE);
        return;
    }
    if (override.rfind("error:", 0U) == 0U) {
        const std::string message =
            override.size() > 6U
                ? override.substr(6U)
                : std::string("Software update failed");
        xapp_status_icon_set_icon_name(
            state->icon, "infiltrator-software-error-symbolic");
        xapp_status_icon_set_tooltip_text(
            state->icon, message.c_str());
        xapp_status_icon_set_visible(state->icon, TRUE);
        return;
    }

    if (state->checking) {
        xapp_status_icon_set_icon_name(
            state->icon, "infiltrator-software-checking-symbolic");
        xapp_status_icon_set_tooltip_text(
            state->icon, "Checking for software updates");
    } else if (!state->last_error.empty()) {
        xapp_status_icon_set_icon_name(
            state->icon, "infiltrator-software-error-symbolic");
        xapp_status_icon_set_tooltip_text(
            state->icon, state->last_error.c_str());
    } else if (state->update_count > 0U) {
        xapp_status_icon_set_icon_name(
            state->icon,
            "infiltrator-software-updates-available-symbolic");
        const std::string tooltip =
            std::to_string(state->update_count) +
            (state->update_count == 1U
                 ? " software update is available"
                 : " software updates are available");
        xapp_status_icon_set_tooltip_text(
            state->icon, tooltip.c_str());
    } else {
        xapp_status_icon_set_icon_name(
            state->icon, "infiltrator-software-up-to-date-symbolic");
        xapp_status_icon_set_tooltip_text(
            state->icon, "Your system is up to date");
    }

    const bool visible =
        !(state->preferences.hide_tray &&
          !state->checking &&
          state->last_error.empty() &&
          state->update_count == 0U);
    xapp_status_icon_set_visible(
        state->icon,
        visible ? TRUE : FALSE);
}

void check_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *result = new CheckResult{};
    auto *data = static_cast<CheckTaskData *>(task_data);

    EngineClient engine;
    if (data != nullptr && data->refresh_metadata) {
        (void)engine.refresh(result->error);
    } else {
        /*
         * Reconcile authoritative dpkg state before reading the cached
         * candidate generation.  Root automatic updates use a separate
         * resolver database, so the tray must not keep advertising packages
         * that were already installed outside this user service.
         */
        (void)engine.refresh_installed(result->error);
    }
    if (result->error.empty()) {
        (void)engine.list_updates(
            result->updates,
            result->error);
    }

    SoftwarePreferences preferences;
    std::string preference_error;
    if (load_software_preferences(
            preferences,
            preference_error)) {
        result->updates.erase(
            std::remove_if(
                result->updates.begin(),
                result->updates.end(),
                [&](const PackageRecord &package) {
                    return update_is_ignored(
                        package, preferences);
                }),
            result->updates.end());
    }

    if (result->error.empty()) {
        std::vector<ExternalUpdate> external;
        std::string external_error;
        if (preferences.show_flatpak_updates) {
            if (!discover_flatpak_updates(
                    external,
                    external_error)) {
                result->error = external_error;
            } else {
                result->external_update_count += external.size();
            }
        }
        if (result->error.empty() &&
            preferences.show_cinnamon_updates) {
            external.clear();
            if (!discover_cinnamon_updates(
                    external,
                    external_error)) {
                result->error = external_error;
            } else {
                result->external_update_count += external.size();
            }
        }
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer value) {
            delete static_cast<CheckResult *>(value);
        });
}

void check_complete(
    GObject *,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *state = static_cast<TrayState *>(user_data);
    if (state == nullptr) {
        return;
    }

    auto *task = G_TASK(async_result);
    auto *data = static_cast<CheckTaskData *>(
        g_task_get_task_data(task));
    auto *result = static_cast<CheckResult *>(
        g_task_propagate_pointer(task, nullptr));
    state->checking = false;

    if (data != nullptr &&
        data->refresh_metadata &&
        result != nullptr &&
        result->error.empty()) {
        state->last_metadata_refresh_us =
            g_get_monotonic_time();
    }

    if (result != nullptr) {
        state->update_count =
            result->updates.size() +
            result->external_update_count;
        state->last_error = result->error;
        if (state->last_error.empty()) {
            evaluate_notification(
                state,
                result->updates,
                state->update_count);
        }
        delete result;
    } else {
        state->last_error = "Unable to check for software updates";
    }

    render(state);
}

void begin_check(
    TrayState *state,
    const bool refresh_metadata = false)
{
    if (state == nullptr || state->checking) {
        return;
    }
    (void)refresh_runtime_inputs(state);
    if (!state->override_state.empty()) {
        render(state);
        return;
    }

    state->checking = true;
    state->last_error.clear();
    render(state);

    GTask *task =
        g_task_new(nullptr, nullptr, check_complete, state);
    auto *task_data = new CheckTaskData{};
    task_data->refresh_metadata = refresh_metadata;
    g_task_set_task_data(
        task,
        task_data,
        [](gpointer value) {
            delete static_cast<CheckTaskData *>(value);
        });
    g_task_run_in_thread(task, check_worker);
    g_object_unref(task);
}

void engine_signal(
    GDBusConnection *,
    const gchar *,
    const gchar *,
    const gchar *,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    auto *state = static_cast<TrayState *>(user_data);
    if (state == nullptr || signal_name == nullptr) {
        return;
    }

    if (std::string_view(signal_name) == "HealthChanged") {
        gboolean healthy = FALSE;
        const gchar *detail = nullptr;
        g_variant_get(
            parameters, "(b&s)", &healthy, &detail);
        if (!healthy) {
            state->last_error =
                detail == nullptr || *detail == '\0'
                    ? "Package engine state is unavailable"
                    : detail;
            render(state);
            return;
        }
        state->last_error.clear();
    }

    /*
     * StateChanged is the normal update path: the tray consumes the same
     * generation as Software instead of independently scheduling another
     * resolver. Health recovery also re-reads the shared snapshot.
     */
    begin_check(state, false);
}

void subscribe_engine(TrayState *state)
{
    if (state == nullptr || state->engine_connection != nullptr) {
        return;
    }

    GError *error = nullptr;
    state->engine_connection =
        g_bus_get_sync(
            G_BUS_TYPE_SESSION,
            nullptr,
            &error);
    if (state->engine_connection == nullptr) {
        if (error != nullptr) {
            g_debug(
                "Unable to subscribe to package-engine state: %s",
                error->message);
            g_error_free(error);
        }
        return;
    }

    state->state_signal_id =
        g_dbus_connection_signal_subscribe(
            state->engine_connection,
            "net.ssmith.infiltrator.software.Engine",
            "net.ssmith.infiltrator.software.Engine",
            "StateChanged",
            "/net/ssmith/infiltrator/software/Engine",
            nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            engine_signal,
            state,
            nullptr);
    state->health_signal_id =
        g_dbus_connection_signal_subscribe(
            state->engine_connection,
            "net.ssmith.infiltrator.software.Engine",
            "net.ssmith.infiltrator.software.Engine",
            "HealthChanged",
            "/net/ssmith/infiltrator/software/Engine",
            nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            engine_signal,
            state,
            nullptr);
}

gboolean scheduled_check(gpointer user_data)
{
    auto *state =
        static_cast<TrayState *>(user_data);
    if (state == nullptr) {
        return G_SOURCE_CONTINUE;
    }

    (void)refresh_runtime_inputs(state);

    if (!state->preferences.refresh_schedule_enabled) {
        return G_SOURCE_CONTINUE;
    }

    const gint64 now = g_get_monotonic_time();
    const gint64 minute_us =
        static_cast<gint64>(G_USEC_PER_SEC) * 60;
    const gint64 required =
        state->last_metadata_refresh_us == 0
            ? static_cast<gint64>(
                  state->preferences.first_refresh_minutes) *
                  minute_us
            : static_cast<gint64>(
                  state->preferences.recurring_refresh_minutes) *
                  minute_us;
    const gint64 reference =
        state->last_metadata_refresh_us == 0
            ? state->started_us
            : state->last_metadata_refresh_us;

    if (reference == 0 || now - reference >= required) {
        begin_check(state, true);
    }
    return G_SOURCE_CONTINUE;
}

bool monitored_event_matches(
    GFile *file,
    GFile *other_file,
    const std::filesystem::path &target)
{
    const auto matches =
        [&](GFile *candidate) {
            if (candidate == nullptr) {
                return false;
            }
            gchar *raw_path = g_file_get_path(candidate);
            if (raw_path == nullptr) {
                return false;
            }
            const bool same =
                std::filesystem::path(raw_path) == target;
            g_free(raw_path);
            return same;
        };
    return matches(file) || matches(other_file);
}

void replace_running_tray_if_needed(TrayState *state)
{
    if (state == nullptr || state->replacement_exec_failed ||
        !installed_tray_replaced()) {
        return;
    }

    /*
     * dpkg replaces the executable atomically. React to the directory event
     * instead of polling /proc and /usr/bin every two seconds for the entire
     * desktop session. The lock fd is O_CLOEXEC, so the replacement image
     * reacquires the normal single-instance lock.
     */
    (void)execl(
        kInstalledTrayPath,
        "infiltrator-software-tray",
        "--replace",
        static_cast<char *>(nullptr));
    state->replacement_exec_failed = true;
    g_warning(
        "Unable to replace stale Software tray executable: %s",
        g_strerror(errno));
}

void runtime_file_changed(
    GFileMonitor *,
    GFile *file,
    GFile *other_file,
    GFileMonitorEvent,
    gpointer user_data)
{
    auto *state = static_cast<TrayState *>(user_data);
    if (state == nullptr) {
        return;
    }

    const std::filesystem::path preferences =
        software_preferences_path();
    const std::filesystem::path override = state_file();
    if (!monitored_event_matches(file, other_file, preferences) &&
        !monitored_event_matches(file, other_file, override)) {
        return;
    }

    if (refresh_runtime_inputs(state)) {
        render(state);
    }
}

void installed_executable_changed(
    GFileMonitor *,
    GFile *file,
    GFile *other_file,
    GFileMonitorEvent,
    gpointer user_data)
{
    if (!monitored_event_matches(
            file,
            other_file,
            std::filesystem::path(kInstalledTrayPath))) {
        return;
    }
    replace_running_tray_if_needed(
        static_cast<TrayState *>(user_data));
}

GFileMonitor *monitor_parent_directory(
    const std::filesystem::path &target,
    GCallback callback,
    TrayState *state)
{
    if (target.empty() || target.parent_path().empty()) {
        return nullptr;
    }

    GFile *directory =
        g_file_new_for_path(target.parent_path().c_str());
    if (directory == nullptr) {
        return nullptr;
    }

    GError *error = nullptr;
    GFileMonitor *monitor =
        g_file_monitor_directory(
            directory,
            G_FILE_MONITOR_NONE,
            nullptr,
            &error);
    g_object_unref(directory);
    if (monitor == nullptr) {
        if (error != nullptr) {
            g_debug(
                "Unable to monitor %s: %s",
                target.parent_path().c_str(),
                error->message);
            g_error_free(error);
        }
        return nullptr;
    }

    g_signal_connect(monitor, "changed", callback, state);
    return monitor;
}

void install_file_monitors(TrayState *state)
{
    if (state == nullptr) {
        return;
    }

    state->preferences_monitor =
        monitor_parent_directory(
            software_preferences_path(),
            G_CALLBACK(runtime_file_changed),
            state);
    state->override_monitor =
        monitor_parent_directory(
            state_file(),
            G_CALLBACK(runtime_file_changed),
            state);
    state->executable_monitor =
        monitor_parent_directory(
            std::filesystem::path(kInstalledTrayPath),
            G_CALLBACK(installed_executable_changed),
            state);
}

gboolean clear_opening_software(gpointer user_data)
{
    auto *state = static_cast<TrayState *>(user_data);
    if (state != nullptr) {
        state->opening_software = false;
        state->opening_reset_id = 0U;
    }
    return G_SOURCE_REMOVE;
}

void open_software(TrayState *state)
{
    if (state == nullptr || state->opening_software) {
        return;
    }

    state->opening_software = true;
    GError *error = nullptr;
    if (!g_spawn_command_line_async(
            "infiltrator-software --updates", &error)) {
        state->opening_software = false;
        if (error != nullptr) {
            g_warning("Unable to launch Software: %s", error->message);
            g_error_free(error);
        }
        return;
    }

    state->opening_reset_id =
        g_timeout_add(
            2500U,
            clear_opening_software,
            state);
}

void icon_activated(
    XAppStatusIcon *,
    guint,
    guint,
    gpointer user_data)
{
    open_software(
        static_cast<TrayState *>(user_data));
}

void open_menu_item(GtkMenuItem *, gpointer user_data)
{
    open_software(
        static_cast<TrayState *>(user_data));
}

void check_menu_item(GtkMenuItem *, gpointer user_data)
{
    begin_check(
        static_cast<TrayState *>(user_data),
        true);
}

void quit_menu_item(GtkMenuItem *, gpointer)
{
    gtk_main_quit();
}

} // namespace

int main(int argc, char **argv)
{
    const bool replace_existing =
        take_replace_argument(argc, argv);
    gtk_init(&argc, &argv);

    const int lock_fd =
        acquire_single_instance_lock(replace_existing);
    if (lock_fd == -2) {
        return 0;
    }
    if (lock_fd < 0) {
        g_warning("Unable to establish update-indicator single-instance lock.");
        return 1;
    }

    TrayState state;
    state.started_us = g_get_monotonic_time();
    (void)refresh_runtime_inputs(&state, true);
    state.icon =
        xapp_status_icon_new_with_name("infiltrator-software-updater");
    if (state.icon == nullptr) {
        return 1;
    }

    GtkWidget *menu = gtk_menu_new();
    GtkWidget *open = gtk_menu_item_new_with_label("Open Software");
    GtkWidget *check = gtk_menu_item_new_with_label("Check for updates");
    GtkWidget *separator = gtk_separator_menu_item_new();
    GtkWidget *quit = gtk_menu_item_new_with_label("Quit update indicator");

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), open);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), check);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
    gtk_widget_show_all(menu);

    xapp_status_icon_set_secondary_menu(
        state.icon, GTK_MENU(menu));
    g_signal_connect(
        state.icon, "activate",
        G_CALLBACK(icon_activated), &state);
    g_signal_connect(
        open, "activate",
        G_CALLBACK(open_menu_item), &state);
    g_signal_connect(
        check, "activate",
        G_CALLBACK(check_menu_item), &state);
    g_signal_connect(
        quit, "activate",
        G_CALLBACK(quit_menu_item), &state);

    subscribe_engine(&state);
    install_file_monitors(&state);
    render(&state);
    g_idle_add(
        [](gpointer data) -> gboolean {
            begin_check(
                static_cast<TrayState *>(data),
                false);
            return G_SOURCE_REMOVE;
        },
        &state);
    g_timeout_add_seconds(60U, scheduled_check, &state);

    gtk_main();

    if (state.opening_reset_id != 0U) {
        g_source_remove(state.opening_reset_id);
    }
    if (state.preferences_monitor != nullptr) {
        g_object_unref(state.preferences_monitor);
    }
    if (state.override_monitor != nullptr) {
        g_object_unref(state.override_monitor);
    }
    if (state.executable_monitor != nullptr) {
        g_object_unref(state.executable_monitor);
    }
    if (state.engine_connection != nullptr) {
        if (state.state_signal_id != 0U) {
            g_dbus_connection_signal_unsubscribe(
                state.engine_connection,
                state.state_signal_id);
        }
        if (state.health_signal_id != 0U) {
            g_dbus_connection_signal_unsubscribe(
                state.engine_connection,
                state.health_signal_id);
        }
        g_object_unref(state.engine_connection);
    }
    g_object_unref(state.icon);
    close(lock_fd);
    return 0;
}
