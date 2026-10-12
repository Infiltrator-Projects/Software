// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/discover_view.hpp"
#include "app/updates_view.hpp"

#include "app/discover_state.hpp"
#include "app/updates_controller.hpp"
#include "app/window_state.hpp"

#include <cstring>
#include <string>

namespace infiltrator::software::app {

void set_discover_search_query(
    WindowState *state,
    const std::string_view query)
{
    if (state == nullptr || state->discover.discover_search == nullptr) {
        return;
    }

    const char *current = gtk_editable_get_text(
        GTK_EDITABLE(state->discover.discover_search));
    const std::string requested(query);
    if (g_strcmp0(current, requested.c_str()) == 0) {
        return;
    }

    state->search_syncing = true;
    gtk_editable_set_text(
        GTK_EDITABLE(state->discover.discover_search),
        requested.c_str());
    state->search_syncing = false;
}

void set_discover_health_state(
    WindowState *state,
    const bool healthy)
{
    if (state == nullptr) {
        return;
    }

    if (state->discover.discover_health_summary != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_health_summary),
            healthy ? "Good" : "Attention");
    }
    if (state->discover.discover_health_banner_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_health_banner_state),
            healthy
                ? "No package-state issues detected."
                : "Repair attention is required.");
    }
}

void destroy_discover_page(WindowState *state)
{
    if (state == nullptr) return;

    if (state->discover.discover_categories != nullptr) {
        g_object_unref(state->discover.discover_categories);
        state->discover.discover_categories = nullptr;
    }
    if (state->discover.discover_visible != nullptr) {
        g_object_unref(state->discover.discover_visible);
        state->discover.discover_visible = nullptr;
    }
}

void set_updates_page_selected(WindowState *, const bool)
{
    /*
     * Selection lifecycle is owned by the page descriptor/coordinator.  Keep
     * this boundary as a compatibility no-op until all older callers have
     * migrated to select_app_page().
     */
}

void select_update_preset(
    WindowState *state,
    const UpdateSelectionPreset preset)
{
    if (state == nullptr) return;

    switch (preset) {
    case UpdateSelectionPreset::all:
        updates_controller_set_pending_selection(state->updates, "");
        break;
    case UpdateSelectionPreset::security:
        updates_controller_set_pending_selection(state->updates, "security");
        break;
    case UpdateSelectionPreset::kernel:
        updates_controller_set_pending_selection(state->updates, "kernel");
        break;
    }

    /*
     * An already hydrated page needs one local-state refresh so the preset is
     * applied immediately.  An unhydrated page consumes the pending preset
     * during its normal first refresh after navigation.
     */
    if (updates_controller_loaded(state->updates) &&
        !updates_controller_busy(state->updates)) {
        refresh_updates(state, false);
    }
}

void destroy_updates_page(WindowState *state)
{
    if (state == nullptr) return;
    destroy_updates_controller(state->updates);
}

} // namespace infiltrator::software::app
