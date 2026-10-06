// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/navigation_controller.hpp"

#include "app/app_coordinator.hpp"
#include "app/app_shell_contract.hpp"
#include "app/discover_state.hpp"
#include "app/history_controller.hpp"
#include "app/installed_controller.hpp"
#include "app/preferences_dialog.hpp"
#include "app/repair_view.hpp"
#include "app/repository_controller.hpp"
#include "app/repository_view.hpp"
#include "app/system_view.hpp"
#include "app/ui_components.hpp"
#include "app/updates_controller.hpp"
#include "app/window_state.hpp"
#include "core/update_freshness.hpp"

#include <cstring>

namespace infiltrator::software::app {

using infiltrator::software::create_history_controller_page;
using infiltrator::software::create_installed_page;
using infiltrator::software::history_controller_loaded;
using infiltrator::software::installed_controller_loaded;
using infiltrator::software::make_icon;
using infiltrator::software::make_label;
using infiltrator::software::refresh_history_controller;
using infiltrator::software::refresh_installed_controller;
using infiltrator::software::repository_controller_loaded;
using infiltrator::software::update_metadata_refresh_due;

namespace {

constexpr int kPageCount = 7;
constexpr const char *kPageNames[kPageCount] = {
    "discover", "installed", "updates", "system",
    "repositories", "history", "repair"
};

void history_controller_changed(gpointer user_data)
{
    notify_history_state_changed(
        static_cast<WindowState *>(user_data));
}

GtkWidget *make_nav_row(
    const char *icon_name,
    const char *text,
    const char *subtitle,
    GtkWidget **badge_out)
{
    GtkWidget *row_box =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    GtkWidget *icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(icon_well, "nav-icon-well");
    GtkWidget *icon = make_icon(icon_name, 27);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_well), icon);
    gtk_box_append(GTK_BOX(row_box), icon_well);

    GtkWidget *copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(copy, true);
    gtk_widget_set_valign(copy, GTK_ALIGN_CENTER);
    GtkWidget *label = make_label(text, "nav-label");
    GtkWidget *sub = make_label(subtitle, "nav-subtitle");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(sub), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(copy), label);
    gtk_box_append(GTK_BOX(copy), sub);
    gtk_box_append(GTK_BOX(row_box), copy);

    if (badge_out != nullptr) {
        GtkWidget *badge = make_label("", "nav-badge", 0.5F);
        gtk_widget_set_valign(badge, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(badge, false);
        gtk_box_append(GTK_BOX(row_box), badge);
        *badge_out = badge;
    }

    GtkWidget *row = gtk_list_box_row_new();
    gtk_widget_add_css_class(row, "nav-row");
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
    return row;
}

gboolean periodic_updates_refresh(gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->stack == nullptr ||
        !updates_controller_loaded(state->updates) ||
        state->updates.updates_busy) {
        return G_SOURCE_CONTINUE;
    }

    const char *visible =
        gtk_stack_get_visible_child_name(state->stack);
    if (visible == nullptr || std::strcmp(visible, "updates") != 0) {
        return G_SOURCE_CONTINUE;
    }

    if (update_metadata_refresh_due(
            g_get_monotonic_time(),
            state->updates.updates_last_metadata_refresh_us,
            state->updates.updates_busy)) {
        refresh_updates_internal(state, true, false);
    }
    return G_SOURCE_CONTINUE;
}

void stop_updates_refresh_timer(WindowState *state)
{
    if (state == nullptr ||
        state->updates.updates_refresh_timer_id == 0U) {
        return;
    }
    g_source_remove(state->updates.updates_refresh_timer_id);
    state->updates.updates_refresh_timer_id = 0U;
}

void start_updates_refresh_timer(WindowState *state)
{
    if (state == nullptr ||
        state->updates.updates_refresh_timer_id != 0U) {
        return;
    }
    state->updates.updates_refresh_timer_id =
        g_timeout_add_seconds(60U, periodic_updates_refresh, state);
}

bool ensure_page_constructed(WindowState *state, const int index)
{
    if (state == nullptr || state->stack == nullptr ||
        index < 0 || index >= kPageCount) {
        return false;
    }
    if (gtk_stack_get_child_by_name(
            state->stack, kPageNames[index]) != nullptr) {
        return true;
    }

    GtkWidget *page = nullptr;
    switch (index) {
    case 0:
        page = make_discover_page(state);
        break;
    case 1:
        page = create_installed_page(
            &state->installed, state->window);
        break;
    case 2:
        page = make_updates_page(state);
        break;
    case 3:
        page = make_system_page(state);
        break;
    case 4:
        page = make_repositories_page(state);
        break;
    case 5:
        page = create_history_controller_page(
            &state->history,
            state->window,
            history_controller_changed,
            state);
        break;
    case 6:
        page = make_repair_page(state);
        break;
    default:
        break;
    }
    if (page == nullptr) {
        return false;
    }

    gtk_stack_add_named(state->stack, page, kPageNames[index]);
    return true;
}

void navigation_changed(
    GtkListBox *, GtkListBoxRow *row, gpointer user_data)
{
    if (row == nullptr || user_data == nullptr) {
        return;
    }

    const int index = gtk_list_box_row_get_index(row);
    if (index < 0 || index >= kPageCount) {
        return;
    }

    auto *state = static_cast<WindowState *>(user_data);
    if (!ensure_page_constructed(state, index)) {
        return;
    }
    if (index == static_cast<int>(AppPage::updates)) {
        start_updates_refresh_timer(state);
    } else {
        stop_updates_refresh_timer(state);
    }
    gtk_stack_set_visible_child_name(state->stack, kPageNames[index]);

    /* Paint the selected page before its asynchronous hydration starts. */
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer data) -> gboolean {
            auto *window = GTK_WINDOW(data);
            auto *idle_state = static_cast<WindowState *>(
                g_object_get_data(
                    G_OBJECT(window),
                    "infiltrator-window-state"));
            if (idle_state == nullptr ||
                idle_state->navigation_list == nullptr) {
                return G_SOURCE_REMOVE;
            }

            GtkListBoxRow *selected =
                gtk_list_box_get_selected_row(
                    idle_state->navigation_list);
            if (selected != nullptr) {
                refresh_page_if_needed(
                    idle_state,
                    gtk_list_box_row_get_index(selected));
            }
            return G_SOURCE_REMOVE;
        },
        g_object_ref(state->window),
        [](gpointer data) {
            g_object_unref(data);
        });
}

} // namespace

void refresh_page_if_needed(WindowState *state, const int index)
{
    if (state == nullptr || !state->window_presented) {
        return;
    }

    switch (index) {
    case 0:
        if (!discover_page_loaded(state->discover)) {
            refresh_discover(state, false);
        }
        break;
    case 1:
        if (!installed_controller_loaded(state->installed)) {
            refresh_installed_controller(&state->installed);
        }
        break;
    case 2:
        if (!updates_controller_loaded(state->updates)) {
            refresh_updates(state, false);
        } else if (update_metadata_refresh_due(
                       g_get_monotonic_time(),
                       state->updates.updates_last_metadata_refresh_us,
                       state->updates.updates_busy)) {
            refresh_updates_internal(state, true, false);
        }
        break;
    case 3:
        if (!state->system.system_busy) {
            refresh_system(state, false);
        }
        break;
    case 4:
        if (!repository_controller_loaded(state->repositories)) {
            refresh_repositories(state);
        }
        break;
    case 5:
        if (!history_controller_loaded(state->history)) {
            refresh_history_controller(&state->history);
        }
        break;
    case 6:
        if (!repair_page_loaded(state->repair)) {
            refresh_repair(state, false);
        }
        break;
    default:
        break;
    }
}

GtkWidget *make_navigation(WindowState *state)
{
    static constexpr const char *labels[kPageCount] = {
        "Discover", "Installed", "Updates", "System",
        "Repositories", "History", "Repair"
    };
    static constexpr const char *subtitles[kPageCount] = {
        "Browse and explore",
        "Manage your software",
        "Available updates",
        "System information",
        "Manage sources",
        "View recent activity",
        "Diagnose and fix issues"
    };
    static constexpr const char *icons[kPageCount] = {
        "go-home-symbolic",
        "view-grid-symbolic",
        "view-refresh-symbolic",
        "computer-symbolic",
        "drive-multidisk-symbolic",
        "document-open-recent-symbolic",
        "applications-engineering-symbolic"
    };

    GtkWidget *outer =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(outer, 195, -1);
    gtk_widget_add_css_class(outer, "sidebar");

    GtkWidget *list = gtk_list_box_new();
    state->navigation_list = GTK_LIST_BOX(list);
    gtk_widget_add_css_class(list, "nav-list");
    gtk_list_box_set_selection_mode(
        GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_widget_set_margin_start(list, 10);
    gtk_widget_set_margin_end(list, 10);
    gtk_widget_set_margin_top(list, 10);
    gtk_widget_set_vexpand(list, true);
    gtk_box_append(GTK_BOX(outer), list);

    for (int index = 0; index < kPageCount; ++index) {
        GtkWidget **badge =
            index == static_cast<int>(AppPage::updates)
                ? &state->nav_updates_badge
                : nullptr;
        gtk_list_box_append(
            GTK_LIST_BOX(list),
            make_nav_row(
                icons[index], labels[index], subtitles[index], badge));
    }

    g_signal_connect(
        list, "row-selected",
        G_CALLBACK(navigation_changed), state);

    GtkWidget *settings = gtk_button_new();
    gtk_widget_add_css_class(settings, "sidebar-settings");
    gtk_widget_set_margin_start(settings, 10);
    gtk_widget_set_margin_end(settings, 10);
    gtk_widget_set_margin_bottom(settings, 14);

    GtkWidget *settings_row =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *settings_icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        settings_icon_well, "settings-icon-well");
    gtk_widget_set_size_request(settings_icon_well, 42, 42);
    GtkWidget *settings_icon =
        make_icon("emblem-system-symbolic", 25);
    gtk_widget_set_halign(settings_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(settings_icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(settings_icon_well), settings_icon);
    gtk_box_append(GTK_BOX(settings_row), settings_icon_well);

    GtkWidget *settings_copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(settings_copy, true);
    gtk_box_append(
        GTK_BOX(settings_copy),
        make_label("Settings", "settings-title"));
    gtk_box_append(
        GTK_BOX(settings_copy),
        make_label("Preferences", "settings-subtitle"));
    gtk_box_append(GTK_BOX(settings_row), settings_copy);

    GtkWidget *settings_chevron =
        make_label("›", "settings-chevron", 0.5F);
    gtk_widget_set_valign(settings_chevron, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(settings_row), settings_chevron);
    gtk_button_set_child(GTK_BUTTON(settings), settings_row);
    g_signal_connect(
        settings, "clicked",
        G_CALLBACK(settings_clicked), state);
    gtk_box_append(GTK_BOX(outer), settings);

    return outer;
}

} // namespace infiltrator::software::app
