// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app_coordinator.hpp"

#include "app/app_shell_contract.hpp"
#include "app/discover_state.hpp"
#include "app/history_controller.hpp"
#include "app/installed_controller.hpp"
#include "app/repair_view.hpp"
#include "app/repository_controller.hpp"
#include "app/system_view.hpp"
#include "app/updates_controller.hpp"
#include "app/window_state.hpp"

namespace infiltrator::software::app {

void notify_kernel_state_changed(WindowState *state)
{
    if (state == nullptr) return;

    if (system_page_loaded(state->system)) {
        refresh_system(state, false);
    }
    if (updates_controller_loaded(state->updates)) {
        refresh_updates(state, false);
    }
    if (infiltrator::software::installed_controller_loaded(
            state->installed)) {
        infiltrator::software::refresh_installed_controller(
            &state->installed);
    }
    if (infiltrator::software::history_controller_loaded(
            state->history)) {
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
    if (updates_controller_loaded(state->updates)) {
        refresh_updates(state, true);
    }
}

void notify_repository_snapshot_changed(WindowState *state)
{
    if (state == nullptr) return;

    rebuild_discover_repository_preview(state);
    set_discover_repository_summary(
        state->discover,
        infiltrator::software::repository_record_count(
            state->repositories));
}

void notify_history_state_changed(WindowState *state)
{
    if (state == nullptr) return;
    rebuild_discover_activity_preview(state);
}

void notify_repair_state_changed(WindowState *state)
{
    if (state == nullptr) return;

    refresh_repair(state, false);
    if (updates_controller_loaded(state->updates)) {
        refresh_updates(state, false);
    }
    if (infiltrator::software::installed_controller_loaded(
            state->installed)) {
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
