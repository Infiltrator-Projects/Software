// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/theme.hpp"
#include "backends/apt/apt_backend.hpp"
#include "core/model.hpp"

#include <gtk/gtk.h>

#include <sstream>
#include <string>
#include <vector>

#ifndef INFILTRATOR_SOFTWARE_VERSION
#define INFILTRATOR_SOFTWARE_VERSION "0.0.0"
#endif

namespace {

using infiltrator::software::AptBackend;
using infiltrator::software::PackageRecord;
using infiltrator::software::ThemeController;

struct WindowState {
    GtkWindow *window{};
    GtkStack *stack{};
    ThemeController theme;
    GtkWidget *theme_button{};
    GtkStringList *installed_strings{};
    GtkWidget *installed_status{};
    GtkWidget *installed_count{};
    GtkWidget *backend_state{};
};

GtkWidget *make_icon(const char *name, int size)
{
    GtkWidget *image = gtk_image_new_from_icon_name(name);
    gtk_image_set_pixel_size(GTK_IMAGE(image), size);
    return image;
}

GtkWidget *make_label(
    const char *text, const char *css_class, float xalign = 0.0F)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), xalign);
    if (css_class != nullptr) {
        gtk_widget_add_css_class(label, css_class);
    }
    return label;
}

GtkWidget *make_stat_card(
    const char *caption,
    const char *value,
    const char *semantic_class,
    GtkWidget **value_out = nullptr)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(card, "stat-card");
    if (semantic_class != nullptr) {
        gtk_widget_add_css_class(card, semantic_class);
    }

    GtkWidget *caption_label = make_label(caption, "stat-caption");
    gtk_box_append(GTK_BOX(card), caption_label);

    GtkWidget *value_label = make_label(value, "stat-value");
    gtk_label_set_ellipsize(GTK_LABEL(value_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(card), value_label);

    if (value_out != nullptr) {
        *value_out = value_label;
    }
    return card;
}

GtkWidget *make_page_intro(
    const char *icon_name, const char *title, const char *subtitle)
{
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class(hero, "page-hero");

    GtkWidget *icon_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(icon_box, "page-icon");
    GtkWidget *icon = make_icon(icon_name, 28);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_box), icon);
    gtk_box_append(GTK_BOX(hero), icon_box);

    GtkWidget *identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *heading = make_label(title, "hero-title");
    GtkWidget *copy = make_label(subtitle, "hero-subtitle");
    gtk_label_set_wrap(GTK_LABEL(copy), true);
    gtk_box_append(GTK_BOX(identity), heading);
    gtk_box_append(GTK_BOX(identity), copy);
    gtk_box_append(GTK_BOX(hero), identity);

    return hero;
}

GtkWidget *make_foundation_page(
    const char *icon_name,
    const char *title,
    const char *subtitle,
    const char *section_title,
    const char *section_copy,
    const char *semantic_class)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    if (semantic_class != nullptr) {
        gtk_widget_add_css_class(page, semantic_class);
    }

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(icon_name, title, subtitle));

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");

    GtkWidget *kicker = make_label("CURRENT MILESTONE", "kicker");
    GtkWidget *heading = make_label(section_title, "card-title");
    GtkWidget *copy = make_label(section_copy, "card-copy");
    gtk_label_set_wrap(GTK_LABEL(copy), true);

    gtk_box_append(GTK_BOX(card), kicker);
    gtk_box_append(GTK_BOX(card), heading);
    gtk_box_append(GTK_BOX(card), copy);
    gtk_box_append(GTK_BOX(page), card);

    return page;
}

void list_item_setup(GtkSignalListItemFactory *, GtkListItem *item, gpointer)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "package-row");

    GtkWidget *icon = make_icon("application-x-executable-symbolic", 20);
    gtk_widget_add_css_class(icon, "package-icon");
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *label = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, true);
    gtk_box_append(GTK_BOX(row), label);

    gtk_list_item_set_child(item, row);
}

void list_item_bind(GtkSignalListItemFactory *, GtkListItem *item, gpointer)
{
    GObject *object = G_OBJECT(gtk_list_item_get_item(item));
    GtkWidget *row = gtk_list_item_get_child(item);
    if (object == nullptr || row == nullptr) {
        return;
    }

    GtkWidget *label = gtk_widget_get_last_child(row);
    if (label == nullptr) {
        return;
    }

    const char *text = gtk_string_object_get_string(GTK_STRING_OBJECT(object));
    gtk_label_set_text(GTK_LABEL(label), text);
}

void refresh_installed(WindowState *state)
{
    if (state == nullptr || state->installed_strings == nullptr) {
        return;
    }

    while (g_list_model_get_n_items(G_LIST_MODEL(state->installed_strings)) > 0U) {
        gtk_string_list_remove(state->installed_strings, 0U);
    }

    AptBackend backend;
    std::string error;
    const std::vector<PackageRecord> packages = backend.list_installed(error);

    for (const PackageRecord &package : packages) {
        const std::string row =
            package.name + "    " + package.installed_version;
        gtk_string_list_append(state->installed_strings, row.c_str());
    }

    if (state->installed_status != nullptr) {
        std::ostringstream message;
        if (!error.empty()) {
            message << "Installed inventory unavailable: " << error;
        } else {
            message << packages.size()
                    << " installed packages reported by the read-only APT backend.";
        }
        gtk_label_set_text(
            GTK_LABEL(state->installed_status), message.str().c_str());
    }

    if (state->installed_count != nullptr) {
        const std::string count = std::to_string(packages.size());
        gtk_label_set_text(GTK_LABEL(state->installed_count), count.c_str());
    }

    if (state->backend_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->backend_state),
            error.empty() ? "Ready" : "Unavailable");
    }
}

GtkWidget *make_installed_page(WindowState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-installed");

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(
            "view-list-symbolic",
            "Installed",
            "Software currently present on this system."));

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card("PACKAGES", "0", "stat-info", &state->installed_count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card("BACKEND", "APT/.deb", "stat-operation"),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card("STATE", "Loading", "stat-success", &state->backend_state),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "card-info");
    gtk_widget_set_vexpand(card, true);

    GtkWidget *heading = make_label("Installed packages", "card-title");
    gtk_box_append(GTK_BOX(card), heading);

    state->installed_status = make_label(
        "Reading installed package inventory…", "card-copy");
    gtk_label_set_wrap(GTK_LABEL(state->installed_status), true);
    gtk_box_append(GTK_BOX(card), state->installed_status);

    state->installed_strings = gtk_string_list_new(nullptr);

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(list_item_setup), nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(list_item_bind), nullptr);

    GtkSelectionModel *selection = GTK_SELECTION_MODEL(
        gtk_single_selection_new(G_LIST_MODEL(state->installed_strings)));
    GtkWidget *list = gtk_list_view_new(selection, factory);
    gtk_widget_add_css_class(list, "package-list");
    gtk_widget_set_vexpand(list, true);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(card), scroll);
    gtk_box_append(GTK_BOX(page), card);

    refresh_installed(state);
    return page;
}

GtkWidget *make_nav_row(
    const char *icon_name, const char *text, const char *semantic_class)
{
    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 11);
    GtkWidget *icon = make_icon(icon_name, 18);
    GtkWidget *label = make_label(text, "nav-label");
    gtk_widget_set_hexpand(label, true);
    gtk_box_append(GTK_BOX(row_box), icon);
    gtk_box_append(GTK_BOX(row_box), label);

    GtkWidget *row = gtk_list_box_row_new();
    gtk_widget_add_css_class(row, "nav-row");
    if (semantic_class != nullptr) {
        gtk_widget_add_css_class(row, semantic_class);
    }
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
    return row;
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
    static constexpr const char *icons[] = {
        "system-search-symbolic",
        "view-list-symbolic",
        "software-update-available-symbolic",
        "computer-symbolic",
        "network-workgroup-symbolic",
        "document-open-recent-symbolic",
        "dialog-warning-symbolic"
    };
    static constexpr const char *semantic_classes[] = {
        "nav-discover", "nav-installed", "nav-updates", "nav-system",
        "nav-repositories", "nav-history", "nav-repair"
    };

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(outer, 248, -1);
    gtk_widget_add_css_class(outer, "sidebar");

    GtkWidget *section = make_label("SOFTWARE", "sidebar-title");
    gtk_widget_set_margin_start(section, 16);
    gtk_widget_set_margin_end(section, 16);
    gtk_widget_set_margin_top(section, 17);
    gtk_widget_set_margin_bottom(section, 9);
    gtk_box_append(GTK_BOX(outer), section);

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "nav-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_widget_set_margin_start(list, 8);
    gtk_widget_set_margin_end(list, 8);
    gtk_widget_set_vexpand(list, true);
    gtk_box_append(GTK_BOX(outer), list);

    for (int i = 0; i < 7; ++i) {
        gtk_list_box_append(
            GTK_LIST_BOX(list),
            make_nav_row(icons[i], labels[i], semantic_classes[i]));
    }

    g_signal_connect(
        list, "row-selected", G_CALLBACK(navigation_changed), state);

    GtkListBoxRow *first =
        gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0);
    gtk_list_box_select_row(GTK_LIST_BOX(list), first);

    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(footer, "sidebar-footer");
    GtkWidget *backend = make_label("APT/.deb backend", "sidebar-note");
    GtkWidget *common = make_label("Common 1.19.10", "sidebar-note");
    gtk_box_append(GTK_BOX(footer), backend);
    gtk_box_append(GTK_BOX(footer), common);
    gtk_box_append(GTK_BOX(outer), footer);

    return outer;
}

void update_theme_button(WindowState *state)
{
    if (state == nullptr || state->theme_button == nullptr) {
        return;
    }

    const std::string label =
        std::string("Theme: ") + state->theme.mode_name();
    gtk_button_set_label(GTK_BUTTON(state->theme_button), label.c_str());
}

void theme_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr) {
        return;
    }

    state->theme.cycle_mode();
    update_theme_button(state);
}

void refresh_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    refresh_installed(state);
}

void about_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr) {
        return;
    }

    GtkWidget *dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(
        GTK_ABOUT_DIALOG(dialog), "Infiltrator Software");
    gtk_about_dialog_set_version(
        GTK_ABOUT_DIALOG(dialog), INFILTRATOR_SOFTWARE_VERSION);
    gtk_about_dialog_set_comments(
        GTK_ABOUT_DIALOG(dialog),
        "Unified software management and updates for the Infiltrator project family.");
    gtk_about_dialog_set_website(
        GTK_ABOUT_DIALOG(dialog),
        "https://github.com/Infiltrator-Projects/Software");
    gtk_about_dialog_set_license_type(
        GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_GPL_3_0);

    static const char *authors[] = {"Shannon Smith", nullptr};
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);

    gtk_window_set_transient_for(GTK_WINDOW(dialog), state->window);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), true);
    gtk_window_set_modal(GTK_WINDOW(dialog), true);
    gtk_window_present(GTK_WINDOW(dialog));
}

GtkWidget *make_header_bar(WindowState *state)
{
    GtkWidget *bar = gtk_header_bar_new();
    gtk_widget_add_css_class(bar, "infiltrator-titlebar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(bar), true);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(title_box, GTK_ALIGN_CENTER);

    GtkWidget *title = make_label(
        "Infiltrator Software", "titlebar-title", 0.5F);
    GtkWidget *subtitle = make_label(
        "Software management & updates", "titlebar-subtitle", 0.5F);
    gtk_box_append(GTK_BOX(title_box), title);
    gtk_box_append(GTK_BOX(title_box), subtitle);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(bar), title_box);

    GtkWidget *refresh =
        gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(
        refresh, "Refresh installed software information");
    gtk_widget_add_css_class(refresh, "titlebar-button");
    g_signal_connect(
        refresh, "clicked", G_CALLBACK(refresh_clicked), state);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), refresh);

    state->theme_button = gtk_button_new_with_label("Theme: System");
    gtk_widget_set_tooltip_text(
        state->theme_button,
        "Cycle Follow OS, Day and Night themes");
    gtk_widget_add_css_class(state->theme_button, "titlebar-button");
    g_signal_connect(
        state->theme_button, "clicked", G_CALLBACK(theme_clicked), state);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), state->theme_button);

    GtkWidget *about = gtk_button_new_from_icon_name("help-about-symbolic");
    gtk_widget_set_tooltip_text(about, "About Infiltrator Software");
    gtk_widget_add_css_class(about, "titlebar-button");
    g_signal_connect(
        about, "clicked", G_CALLBACK(about_clicked), state);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), about);

    update_theme_button(state);
    return bar;
}

GtkWidget *make_status_bar()
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(bar, "statusbar");

    GtkWidget *ready = make_label("Ready", "statusbar-text");
    gtk_widget_set_hexpand(ready, true);
    gtk_box_append(GTK_BOX(bar), ready);

    const std::string version =
        std::string("Version ") + INFILTRATOR_SOFTWARE_VERSION;
    GtkWidget *version_label =
        make_label(version.c_str(), "statusbar-text", 1.0F);
    gtk_box_append(GTK_BOX(bar), version_label);

    return bar;
}

void destroy_window_state(gpointer data)
{
    auto *state = static_cast<WindowState *>(data);
    if (state == nullptr) {
        return;
    }

    if (state->installed_strings != nullptr) {
        g_object_unref(state->installed_strings);
        state->installed_strings = nullptr;
    }
    delete state;
}

void activate(GtkApplication *application, gpointer)
{
    GtkWidget *window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Infiltrator Software");
    gtk_window_set_default_size(GTK_WINDOW(window), 1220, 780);
    gtk_widget_set_size_request(window, 940, 620);

    auto *state = new WindowState{};
    state->window = GTK_WINDOW(window);
    state->theme.initialise();

    g_object_set_data_full(
        G_OBJECT(window), "infiltrator-window-state",
        state, destroy_window_state);

    GtkWidget *header = make_header_bar(state);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), root);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(body, true);
    gtk_box_append(GTK_BOX(root), body);

    GtkWidget *stack = gtk_stack_new();
    state->stack = GTK_STACK(stack);
    gtk_stack_set_transition_type(
        state->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(stack, true);
    gtk_widget_set_vexpand(stack, true);

    gtk_box_append(GTK_BOX(body), make_navigation(state));
    gtk_box_append(GTK_BOX(body), stack);

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "system-search-symbolic",
            "Discover",
            "Find software from verified repositories.",
            "Catalogue foundation",
            "Authoritative repository metadata, application identity and icon "
            "provenance are the next functional milestone. The UI is already "
            "separated from package-manager mechanics.",
            "page-discover"),
        "discover");

    gtk_stack_add_named(
        state->stack,
        make_installed_page(state),
        "installed");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "software-update-available-symbolic",
            "Updates",
            "Application and system updates in one place.",
            "Transaction planning first",
            "Updates remain read-only until dependency resolution, complete "
            "change-set presentation and privilege separation are proven.",
            "page-updates"),
        "updates");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "computer-symbolic",
            "System",
            "Kernels, drivers and core operating-system components.",
            "System changes stay distinct",
            "System-critical updates will remain visually and operationally "
            "distinct without forcing a second updater application.",
            "page-system"),
        "system");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "network-workgroup-symbolic",
            "Repositories",
            "Sources, priorities and Stable/Beta/Alpha channels.",
            "Repository policy",
            "Repository and channel management will be exposed here without "
            "requiring manual editing of package-source files.",
            "page-repositories"),
        "repositories");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "document-open-recent-symbolic",
            "History",
            "Exact software-management operations and outcomes.",
            "Durable transaction history",
            "Future write operations will record exact before/after versions, "
            "source provenance and recovery linkage.",
            "page-history"),
        "history");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "dialog-warning-symbolic",
            "Repair",
            "Diagnose interrupted package and repository state.",
            "Repair is explicit",
            "Broken dependencies, interrupted transactions and inconsistent "
            "repository state will be diagnosed here rather than hidden.",
            "page-repair"),
        "repair");

    gtk_stack_set_visible_child_name(state->stack, "discover");
    gtk_box_append(GTK_BOX(root), make_status_bar());

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
