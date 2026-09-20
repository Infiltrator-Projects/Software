// SPDX-License-Identifier: GPL-3.0-or-later
#include "backends/apt/apt_backend.hpp"

#include <gtk/gtk.h>
#include <xapp/xapp-status-icon.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using infiltrator::software::AptBackend;
using infiltrator::software::PackageRecord;

struct CheckResult {
    std::vector<PackageRecord> updates;
    std::string error;
};

struct TrayState {
    XAppStatusIcon *icon{};
    bool checking{false};
    std::size_t update_count{0U};
    std::string last_error;
};

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
    AptBackend backend;
    result->updates = backend.list_updates(result->error);
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

void open_software()
{
    GError *error = nullptr;
    if (!g_spawn_command_line_async(
            "infiltrator-software --updates", &error)) {
        if (error != nullptr) {
            g_warning("Unable to launch Software: %s", error->message);
            g_error_free(error);
        }
    }
}

void icon_activated(
    XAppStatusIcon *,
    guint,
    guint,
    gpointer)
{
    open_software();
}

void open_menu_item(GtkMenuItem *, gpointer)
{
    open_software();
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

    g_object_unref(state.icon);
    return 0;
}
