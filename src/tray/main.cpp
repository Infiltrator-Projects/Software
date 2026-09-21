// SPDX-License-Identifier: GPL-3.0-or-later
#include "backends/apt/apt_backend.hpp"
#include "client/engine_client.hpp"

#include <gtk/gtk.h>
#include <libxapp/xapp-status-icon.h>

#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using infiltrator::software::AptBackend;
using infiltrator::software::EngineClient;
using infiltrator::software::PackageRecord;

struct CheckResult {
    std::vector<PackageRecord> updates;
    std::string error;
    bool from_engine{false};
};

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
};

int acquire_single_instance_lock()
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
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return -2;
    }
    return fd;
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

void render(TrayState *state)
{
    if (state == nullptr || state->icon == nullptr) {
        return;
    }

    const std::string override = read_override();
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

    xapp_status_icon_set_visible(state->icon, TRUE);
}

void check_worker(
    GTask *task,
    gpointer,
    gpointer,
    GCancellable *)
{
    auto *result = new CheckResult{};

    EngineClient engine;
    std::string engine_error;
    if (engine.list_updates(
            result->updates,
            engine_error)) {
        result->from_engine = true;
    } else {
        AptBackend fallback;
        result->updates =
            fallback.list_updates(result->error);
        if (!result->error.empty() &&
            !engine_error.empty()) {
            result->error =
                "Shared engine unavailable: " +
                engine_error +
                " Compatibility update scan failed: " +
                result->error;
        }
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer data) {
            delete static_cast<CheckResult *>(data);
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

    auto *result = static_cast<CheckResult *>(
        g_task_propagate_pointer(G_TASK(async_result), nullptr));
    state->checking = false;

    if (result != nullptr) {
        state->update_count = result->updates.size();
        state->last_error = result->error;
        delete result;
    } else {
        state->last_error = "Unable to check for software updates";
    }

    render(state);
}

void begin_check(TrayState *state)
{
    if (state == nullptr || state->checking) {
        return;
    }
    if (!read_override().empty()) {
        render(state);
        return;
    }

    state->checking = true;
    state->last_error.clear();
    render(state);

    GTask *task =
        g_task_new(nullptr, nullptr, check_complete, state);
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
    begin_check(state);
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
    begin_check(static_cast<TrayState *>(user_data));
    return G_SOURCE_CONTINUE;
}

gboolean state_tick(gpointer user_data)
{
    render(static_cast<TrayState *>(user_data));
    return G_SOURCE_CONTINUE;
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
    begin_check(static_cast<TrayState *>(user_data));
}

void quit_menu_item(GtkMenuItem *, gpointer)
{
    gtk_main_quit();
}

} // namespace

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    const int lock_fd = acquire_single_instance_lock();
    if (lock_fd == -2) {
        return 0;
    }
    if (lock_fd < 0) {
        g_warning("Unable to establish update-indicator single-instance lock.");
        return 1;
    }

    TrayState state;
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
    render(&state);
    g_idle_add(
        [](gpointer data) -> gboolean {
            begin_check(static_cast<TrayState *>(data));
            return G_SOURCE_REMOVE;
        },
        &state);
    g_timeout_add_seconds(600U, scheduled_check, &state);
    g_timeout_add_seconds(2U, state_tick, &state);

    gtk_main();

    if (state.opening_reset_id != 0U) {
        g_source_remove(state.opening_reset_id);
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
