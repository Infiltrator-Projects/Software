// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/navigation_controller.hpp"

#include "app/app_coordinator.hpp"
#include "app/page_registry.hpp"
#include "app/preferences_dialog.hpp"
#include "app/ui_components.hpp"
#include "app/window_state.hpp"

namespace infiltrator::software::app {

using infiltrator::software::make_icon;
using infiltrator::software::make_label;

namespace {

GtkWidget *make_nav_row(
    const PageDescriptor &page,
    GtkWidget **badge_out)
{
    GtkWidget *row_box =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    GtkWidget *icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(icon_well, "nav-icon-well");
    GtkWidget *icon = make_icon(page.icon_name, 27);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_well), icon);
    gtk_box_append(GTK_BOX(row_box), icon_well);

    GtkWidget *copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(copy, true);
    gtk_widget_set_valign(copy, GTK_ALIGN_CENTER);
    GtkWidget *label = make_label(page.label, "nav-label");
    GtkWidget *sub = make_label(page.subtitle, "nav-subtitle");
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

bool ensure_page_constructed(WindowState *state, const int index)
{
    if (state == nullptr || state->stack == nullptr) {
        return false;
    }
    const PageDescriptor *descriptor = page_descriptor(index);
    if (descriptor == nullptr) {
        return false;
    }
    if (gtk_stack_get_child_by_name(
            state->stack, descriptor->name) != nullptr) {
        return true;
    }

    GtkWidget *page = descriptor->construct(state);
    if (page == nullptr) {
        return false;
    }
    gtk_stack_add_named(state->stack, page, descriptor->name);
    return true;
}

void apply_selection_lifecycle(WindowState *state, const AppPage selected)
{
    for (const PageDescriptor &descriptor : page_descriptors()) {
        descriptor.selection_changed(
            state,
            descriptor.page == selected);
    }
}

void navigation_changed(
    GtkListBox *, GtkListBoxRow *row, gpointer user_data)
{
    if (row == nullptr || user_data == nullptr) {
        return;
    }

    const int index = gtk_list_box_row_get_index(row);
    const PageDescriptor *descriptor = page_descriptor(index);
    auto *state = static_cast<WindowState *>(user_data);
    if (descriptor == nullptr ||
        !ensure_page_constructed(state, index)) {
        return;
    }

    apply_selection_lifecycle(state, descriptor->page);
    gtk_stack_set_visible_child_name(state->stack, descriptor->name);

    /* Paint the selected page before its asynchronous hydration starts. */
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer data) -> gboolean {
            auto *window = GTK_WINDOW(data);
            auto *idle_state = static_cast<WindowState *>(
                g_object_get_data(
                    G_OBJECT(window),
                    kWindowStateDataKey));
            refresh_selected_page(idle_state);
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
    const PageDescriptor *descriptor = page_descriptor(index);
    if (descriptor != nullptr) {
        descriptor->refresh_if_needed(state);
    }
}

void refresh_selected_page(WindowState *state)
{
    if (state == nullptr || state->navigation_list == nullptr) {
        return;
    }
    GtkListBoxRow *selected =
        gtk_list_box_get_selected_row(state->navigation_list);
    if (selected != nullptr) {
        refresh_page_if_needed(
            state,
            gtk_list_box_row_get_index(selected));
    }
}

GtkWidget *make_navigation(WindowState *state)
{
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

    for (const PageDescriptor &descriptor : page_descriptors()) {
        GtkWidget **badge =
            descriptor.page == AppPage::updates
                ? &state->nav_updates_badge
                : nullptr;
        gtk_list_box_append(
            GTK_LIST_BOX(list),
            make_nav_row(descriptor, badge));
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
