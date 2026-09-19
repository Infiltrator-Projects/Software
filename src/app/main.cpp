// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/theme.hpp"
#include "backends/apt/apt_backend.hpp"
#include "core/model.hpp"

#include <gtk/gtk.h>

#include <sstream>
#include <string>
#include <vector>

namespace {

using infiltrator::software::AptBackend;
using infiltrator::software::PackageRecord;

struct WindowState {
    GtkStack *stack{};
    infiltrator::software::ThemeController theme;
};

GtkWidget *make_heading(const char *title, const char *description)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(box, "content");

    GtkWidget *heading = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0F);
    gtk_widget_add_css_class(heading, "section-title");
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *body = gtk_label_new(description);
    gtk_label_set_xalign(GTK_LABEL(body), 0.0F);
    gtk_label_set_wrap(GTK_LABEL(body), true);
    gtk_widget_add_css_class(body, "muted");
    gtk_box_append(GTK_BOX(box), body);

    return box;
}

void list_item_setup(GtkSignalListItemFactory *, GtkListItem *item, gpointer)
{
    GtkWidget *label = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 7);
    gtk_widget_set_margin_bottom(label, 7);
    gtk_list_item_set_child(item, label);
}

void list_item_bind(GtkSignalListItemFactory *, GtkListItem *item, gpointer)
{
    GObject *object = G_OBJECT(gtk_list_item_get_item(item));
    GtkWidget *label = gtk_list_item_get_child(item);
    if (object == nullptr || label == nullptr) {
        return;
    }

    const char *text = gtk_string_object_get_string(GTK_STRING_OBJECT(object));
    gtk_label_set_text(GTK_LABEL(label), text);
}

GtkWidget *make_installed_page()
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(box, "content");

    GtkWidget *heading = gtk_label_new("Installed");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0F);
    gtk_widget_add_css_class(heading, "section-title");
    gtk_box_append(GTK_BOX(box), heading);

    AptBackend backend;
    std::string error;
    const std::vector<PackageRecord> packages = backend.list_installed(error);

    std::ostringstream status;
    if (!error.empty()) {
        status << "Installed inventory unavailable: " << error;
    } else {
        status << packages.size()
               << " installed packages reported by the read-only APT backend.";
    }

    GtkWidget *status_label = gtk_label_new(status.str().c_str());
    gtk_label_set_xalign(GTK_LABEL(status_label), 0.0F);
    gtk_widget_add_css_class(status_label, "muted");
    gtk_box_append(GTK_BOX(box), status_label);

    GtkStringList *strings = gtk_string_list_new(nullptr);
    for (const PackageRecord &package : packages) {
        const std::string row =
            package.name + "    " + package.installed_version;
        gtk_string_list_append(strings, row.c_str());
    }

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(list_item_setup), nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(list_item_bind), nullptr);

    GtkSelectionModel *selection = GTK_SELECTION_MODEL(
        gtk_single_selection_new(G_LIST_MODEL(strings)));
    GtkWidget *list = gtk_list_view_new(selection, factory);
    gtk_widget_add_css_class(list, "package-list");

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(box), scroll);

    return box;
}

void navigation_changed(
    GtkListBox *, GtkListBoxRow *row, gpointer user_data)
{
    if (row == nullptr || user_data == nullptr) {
        return;
    }

    static constexpr const char *page_names[] = {
        "discover", "installed", "updates", "system",
        "repositories", "history", "repair"
    };

    const int index = gtk_list_box_row_get_index(row);
    if (index < 0 || index >= 7) {
        return;
    }

    auto *state = static_cast<WindowState *>(user_data);
    gtk_stack_set_visible_child_name(state->stack, page_names[index]);
}

GtkWidget *make_navigation(WindowState *state)
{
    static constexpr const char *labels[] = {
        "Discover", "Installed", "Updates", "System",
        "Repositories", "History", "Repair"
    };

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_size_request(outer, 210, -1);
    gtk_widget_add_css_class(outer, "sidebar");

    GtkWidget *brand = gtk_label_new("INFILTRATOR\nSOFTWARE");
    gtk_label_set_xalign(GTK_LABEL(brand), 0.0F);
    gtk_widget_set_margin_start(brand, 16);
    gtk_widget_set_margin_top(brand, 18);
    gtk_widget_set_margin_bottom(brand, 8);
    gtk_widget_add_css_class(brand, "section-title");
    gtk_box_append(GTK_BOX(outer), brand);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_widget_set_margin_start(list, 8);
    gtk_widget_set_margin_end(list, 8);
    gtk_widget_set_vexpand(list, true);
    gtk_box_append(GTK_BOX(outer), list);

    for (const char *label_text : labels) {
        GtkWidget *label = gtk_label_new(label_text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
        GtkWidget *row = gtk_list_box_row_new();
        gtk_widget_add_css_class(row, "nav-row");
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }

    g_signal_connect(
        list, "row-selected", G_CALLBACK(navigation_changed), state);

    GtkListBoxRow *first =
        gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0);
    gtk_list_box_select_row(GTK_LIST_BOX(list), first);

    gtk_box_append(GTK_BOX(outer), state->theme.create_selector());
    return outer;
}

void destroy_window_state(gpointer data)
{
    delete static_cast<WindowState *>(data);
}

void activate(GtkApplication *application, gpointer)
{
    GtkWidget *window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Infiltrator Software");
    gtk_window_set_default_size(GTK_WINDOW(window), 1120, 720);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), root);

    auto *state = new WindowState{};
    state->theme.initialise();
    g_object_set_data_full(
        G_OBJECT(window), "infiltrator-window-state",
        state, destroy_window_state);

    GtkWidget *stack = gtk_stack_new();
    state->stack = GTK_STACK(stack);
    gtk_stack_set_transition_type(
        state->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(stack, true);
    gtk_widget_set_vexpand(stack, true);

    gtk_box_append(GTK_BOX(root), make_navigation(state));
    gtk_box_append(GTK_BOX(root), stack);

    gtk_stack_add_named(
        state->stack,
        make_heading("Discover",
            "Application discovery will consume authoritative repository "
            "metadata through the backend contract."),
        "discover");
    gtk_stack_add_named(state->stack, make_installed_page(), "installed");
    gtk_stack_add_named(
        state->stack,
        make_heading("Updates",
            "Application and system updates share one surface. "
            "System-critical changes remain explicitly distinguished."),
        "updates");
    gtk_stack_add_named(
        state->stack,
        make_heading("System",
            "Kernels, drivers and core operating-system components live here "
            "rather than being mixed into ordinary application browsing."),
        "system");
    gtk_stack_add_named(
        state->stack,
        make_heading("Repositories",
            "Sources, priorities and Stable/Beta/Alpha channel policy will be "
            "managed here without manual source-file editing."),
        "repositories");
    gtk_stack_add_named(
        state->stack,
        make_heading("History",
            "Every completed transaction will retain exact package versions, "
            "source, result and recovery linkage."),
        "history");
    gtk_stack_add_named(
        state->stack,
        make_heading("Repair",
            "Interrupted transactions, dependency failures and repository "
            "inconsistencies will be diagnosed here."),
        "repair");

    gtk_stack_set_visible_child_name(state->stack, "discover");
    gtk_window_present(GTK_WINDOW(window));
}

} // namespace

int main(int argc, char **argv)
{
    GtkApplication *application = gtk_application_new(
        "net.ssmith.infiltrator.software", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), nullptr);
    const int status =
        g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
