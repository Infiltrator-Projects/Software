// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app_coordinator.hpp"

#include "app/app_shell_contract.hpp"
#include "app/history_controller.hpp"
#include "app/installed_controller.hpp"
#include "app/repair_view.hpp"
#include "app/repository_controller.hpp"
#include "app/system_view.hpp"
#include "app/window_state.hpp"

#include <gtk/gtk.h>

#include <string>

namespace infiltrator::software::app {

void notify_kernel_state_changed(WindowState *state)
{
    if (state == nullptr) return;

    if (state->system_loaded) {
        refresh_system(state, false);
    }
    if (state->updates_loaded) {
        refresh_updates(state, false);
    }
    if (state->installed.loaded) {
        infiltrator::software::refresh_installed_controller(
            &state->installed);
    }
    if (state->history.loaded) {
        infiltrator::software::refresh_history_controller(
            &state->history);
    }
}

void notify_repository_state_changed(WindowState *state)
{
    if (state == nullptr) return;

    infiltrator::software::refresh_repository_controller(
        &state->repositories);
    refresh_discover(state, true);
    if (state->updates_loaded) {
        refresh_updates(state, true);
    }
}

void notify_repository_snapshot_changed(WindowState *state)
{
    if (state == nullptr) return;

    rebuild_discover_repository_preview(state);
    if (state->discover_repositories_summary != nullptr) {
        const std::string count =
            std::to_string(state->repositories.records.size());
        gtk_label_set_text(
            GTK_LABEL(state->discover_repositories_summary),
            count.c_str());
    }
}

void notify_repair_state_changed(WindowState *state)
{
    if (state == nullptr) return;

    refresh_repair(state, false);
    if (state->updates_loaded) {
        refresh_updates(state, false);
    }
    if (state->installed.loaded) {
        infiltrator::software::refresh_installed_controller(
            &state->installed);
    }
}

void select_app_page(WindowState *state, const AppPage page)
{
    if (state == nullptr || state->navigation_list == nullptr) {
        return;
    }

    const int index = static_cast<int>(page);
    GtkListBoxRow *row =
        gtk_list_box_get_row_at_index(
            state->navigation_list,
            index);
    if (row != nullptr) {
        gtk_list_box_select_row(
            state->navigation_list,
            row);
    }
}

} // namespace infiltrator::software::app
