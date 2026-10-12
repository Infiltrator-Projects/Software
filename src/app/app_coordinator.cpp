// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app_coordinator.hpp"
#include "app/page_registry.hpp"
#include "app/view_intents.hpp"
#include "app/window_state.hpp"

namespace infiltrator::software::app {

void notify_kernel_state_changed(WindowState *state) { invalidate_pages(state, AppStateChange::kernel); }
void notify_repository_state_changed(WindowState *state) { invalidate_pages(state, AppStateChange::repository); }
void notify_repository_snapshot_changed(WindowState *state) { invalidate_pages(state, AppStateChange::repository_snapshot); }
void notify_history_state_changed(WindowState *state) { invalidate_pages(state, AppStateChange::history_snapshot); }
void notify_transaction_history_changed(WindowState *state) { invalidate_pages(state, AppStateChange::transaction_history); }
void notify_updates_state_changed(WindowState *state) { invalidate_pages(state, AppStateChange::updates_snapshot); }
void notify_catalogue_transaction_completed(WindowState *state) { invalidate_pages(state, AppStateChange::catalogue_transaction); }
void notify_update_transaction_completed(WindowState *state) { invalidate_pages(state, AppStateChange::update_transaction); }
void notify_repair_state_changed(WindowState *state) { invalidate_pages(state, AppStateChange::repair); }

void notify_repair_health_changed(WindowState *state, const bool healthy)
{
    set_discover_health_state(state, healthy);
}
void set_app_search_query(WindowState *state, const std::string_view query)
{
    set_discover_search_query(state, query);
}
void show_security_updates(WindowState *state)
{
    if (state == nullptr) return;
    select_update_preset(state, UpdateSelectionPreset::security);
    select_app_page(state, AppPage::updates);
}
void show_kernel_updates(WindowState *state)
{
    if (state == nullptr) return;
    select_update_preset(state, UpdateSelectionPreset::kernel);
    select_app_page(state, AppPage::updates);
}
void select_app_page(WindowState *state, const AppPage page)
{
    if (state == nullptr || state->navigation_list == nullptr) return;
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(
        state->navigation_list, static_cast<int>(page));
    if (row != nullptr) gtk_list_box_select_row(state->navigation_list, row);
}

} // namespace infiltrator::software::app
