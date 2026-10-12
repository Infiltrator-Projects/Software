// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app_coordinator.hpp"

#include "app/discover_state.hpp"
#include "app/discover_view.hpp"
#include "app/history_controller.hpp"
#include "app/installed_controller.hpp"
#include "app/page_registry.hpp"
#include "app/repair_view.hpp"
#include "app/repository_controller.hpp"
#include "app/repository_view.hpp"
#include "app/system_view.hpp"
#include "app/updates_controller.hpp"
#include "app/updates_view.hpp"
#include "app/window_state.hpp"

namespace infiltrator::software::app {

using infiltrator::software::create_history_controller_page;
using infiltrator::software::create_installed_page;
using infiltrator::software::history_controller_loaded;
using infiltrator::software::installed_controller_loaded;
using infiltrator::software::refresh_history_controller;
using infiltrator::software::refresh_installed_controller;
using infiltrator::software::repository_controller_loaded;

namespace {

void history_controller_changed(gpointer user_data)
{
    notify_history_state_changed(
        static_cast<WindowState *>(user_data));
}

GtkWidget *construct_discover(WindowState *state)
{
    return make_discover_page(state);
}

GtkWidget *construct_installed(WindowState *state)
{
    return state == nullptr
        ? nullptr
        : create_installed_page(&state->installed, state->window);
}

GtkWidget *construct_updates(WindowState *state)
{
    return make_updates_page(state);
}

GtkWidget *construct_system(WindowState *state)
{
    return make_system_page(state);
}

GtkWidget *construct_repositories(WindowState *state)
{
    return make_repositories_page(state);
}

GtkWidget *construct_history(WindowState *state)
{
    return state == nullptr
        ? nullptr
        : create_history_controller_page(
              &state->history,
              state->window,
              history_controller_changed,
              state);
}

GtkWidget *construct_repair(WindowState *state)
{
    return make_repair_page(state);
}

void refresh_discover_if_needed(WindowState *state)
{
    if (state != nullptr && !discover_page_loaded(state->discover)) {
        refresh_discover(state, false);
    }
}

void refresh_installed_if_needed(WindowState *state)
{
    if (state != nullptr && !installed_controller_loaded(state->installed)) {
        refresh_installed_controller(&state->installed);
    }
}

void refresh_updates_if_needed(WindowState *state)
{
    if (state == nullptr) return;
    if (!updates_controller_loaded(state->updates)) {
        refresh_updates(state, false);
    } else if (updates_controller_metadata_refresh_due(
                   state->updates,
                   g_get_monotonic_time())) {
        refresh_updates_internal(state, true, false);
    }
}

void refresh_system_if_needed(WindowState *state)
{
    if (state != nullptr &&
        !system_page_loaded(state->system) &&
        !system_page_busy(state->system)) {
        refresh_system(state, false);
    }
}

void refresh_repositories_if_needed(WindowState *state)
{
    if (state != nullptr && !repository_controller_loaded(state->repositories)) {
        refresh_repositories(state);
    }
}

void refresh_history_if_needed(WindowState *state)
{
    if (state != nullptr && !history_controller_loaded(state->history)) {
        refresh_history_controller(&state->history);
    }
}

void refresh_repair_if_needed(WindowState *state)
{
    if (state != nullptr && !repair_page_loaded(state->repair)) {
        refresh_repair(state, false);
    }
}

gboolean periodic_updates_refresh(gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr ||
        !updates_controller_loaded(state->updates) ||
        updates_controller_busy(state->updates)) {
        return G_SOURCE_CONTINUE;
    }

    if (updates_controller_metadata_refresh_due(
            state->updates,
            g_get_monotonic_time())) {
        refresh_updates_internal(state, true, false);
    }
    return G_SOURCE_CONTINUE;
}

void updates_selection_changed(WindowState *state, const bool selected)
{
    if (state == nullptr) return;
    updates_controller_set_selected(
        state->updates,
        selected,
        periodic_updates_refresh,
        state);
}

void no_selection_hook(WindowState *, bool) {}

const std::array<PageDescriptor, kAppPageCount> kPages{{
    {AppPage::discover, "discover", "Discover", "Browse and explore",
     "go-home-symbolic", construct_discover,
     refresh_discover_if_needed, no_selection_hook},
    {AppPage::installed, "installed", "Installed", "Manage your software",
     "view-grid-symbolic", construct_installed,
     refresh_installed_if_needed, no_selection_hook},
    {AppPage::updates, "updates", "Updates", "Available updates",
     "view-refresh-symbolic", construct_updates,
     refresh_updates_if_needed, updates_selection_changed},
    {AppPage::system, "system", "System", "System information",
     "computer-symbolic", construct_system,
     refresh_system_if_needed, no_selection_hook},
    {AppPage::repositories, "repositories", "Repositories", "Manage sources",
     "drive-multidisk-symbolic", construct_repositories,
     refresh_repositories_if_needed, no_selection_hook},
    {AppPage::history, "history", "History", "View recent activity",
     "document-open-recent-symbolic", construct_history,
     refresh_history_if_needed, no_selection_hook},
    {AppPage::repair, "repair", "Repair", "Diagnose and fix issues",
     "applications-engineering-symbolic", construct_repair,
     refresh_repair_if_needed, no_selection_hook},
}};

} // namespace

const std::array<PageDescriptor, kAppPageCount> &page_descriptors()
{
    return kPages;
}

const PageDescriptor *page_descriptor(const int index) noexcept
{
    if (index < 0 || static_cast<std::size_t>(index) >= kPages.size()) {
        return nullptr;
    }
    return &kPages[static_cast<std::size_t>(index)];
}

const PageDescriptor *page_descriptor(const AppPage page) noexcept
{
    return page_descriptor(static_cast<int>(page));
}

void notify_kernel_state_changed(WindowState *state)
{
    if (state == nullptr) return;

    if (system_page_loaded(state->system)) {
        refresh_system(state, false);
    }
    if (updates_controller_loaded(state->updates)) {
        refresh_updates(state, false);
    }
    if (installed_controller_loaded(state->installed)) {
        refresh_installed_controller(&state->installed);
    }
    if (history_controller_loaded(state->history)) {
        refresh_history_controller(&state->history);
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

void notify_repair_health_changed(
    WindowState *state,
    const bool healthy)
{
    set_discover_health_state(state, healthy);
}

void notify_repair_state_changed(WindowState *state)
{
    if (state == nullptr) return;

    refresh_repair(state, false);
    if (updates_controller_loaded(state->updates)) {
        refresh_updates(state, false);
    }
    if (installed_controller_loaded(state->installed)) {
        refresh_installed_controller(&state->installed);
    }
}

void select_app_page(WindowState *state, const AppPage page)
{
    if (state == nullptr || state->navigation_list == nullptr) {
        return;
    }

    GtkListBoxRow *row = gtk_list_box_get_row_at_index(
        state->navigation_list,
        static_cast<int>(page));
    if (row != nullptr) {
        gtk_list_box_select_row(state->navigation_list, row);
    }
}

} // namespace infiltrator::software::app
