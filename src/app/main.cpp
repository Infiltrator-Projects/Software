// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app_coordinator.hpp"
#include "app/navigation_controller.hpp"
#include "app/page_registry.hpp"
#include "app/ui_components.hpp"
#include "app/window_state.hpp"
#include "core/update_policy.hpp"

#include <gtk/gtk.h>
#include <infiltratr/core.h>

#include <cstring>
#include <string>
#include <string_view>

#ifndef INFILTRATOR_SOFTWARE_VERSION
#define INFILTRATOR_SOFTWARE_VERSION "0.0.0"
#endif

namespace infiltrator::software::app {
namespace {

static constexpr const char kTitlebarLogoBase64[] =
#include "titlebar_logo.inc"
    ;

GtkWidget *shell_embedded_picture(
    const char *base64,
    const char *css_class,
    const GtkContentFit content_fit)
{
    if (base64 == nullptr || *base64 == '\0') return nullptr;

    gsize image_size = 0;
    guchar *image_data = g_base64_decode(base64, &image_size);
    if (image_data == nullptr || image_size == 0U) {
        g_free(image_data);
        return nullptr;
    }

    GBytes *image_bytes = g_bytes_new_take(image_data, image_size);
    GError *texture_error = nullptr;
    GdkTexture *texture =
        gdk_texture_new_from_bytes(image_bytes, &texture_error);
    g_bytes_unref(image_bytes);
    if (texture == nullptr) {
        g_warning(
            "Embedded titlebar raster failed to decode: %s",
            texture_error != nullptr
                ? texture_error->message
                : "unknown image decoder error");
        g_clear_error(&texture_error);
        return nullptr;
    }

    GtkWidget *picture =
        gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    g_object_unref(texture);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), content_fit);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), true);
    if (css_class != nullptr) gtk_widget_add_css_class(picture, css_class);
    return picture;
}

void shell_update_maximize_button(WindowState *state)
{
    if (state == nullptr || state->window == nullptr ||
        state->maximize_button == nullptr) {
        return;
    }

    const bool maximized = gtk_window_is_maximized(state->window);
    gtk_button_set_icon_name(
        GTK_BUTTON(state->maximize_button),
        maximized ? "window-restore-symbolic" : "window-maximize-symbolic");
    gtk_widget_set_tooltip_text(
        state->maximize_button,
        maximized ? "Restore window" : "Maximize window");
}

void shell_window_maximized_changed(
    GObject *, GParamSpec *, gpointer user_data)
{
    shell_update_maximize_button(static_cast<WindowState *>(user_data));
}

void shell_minimize_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state != nullptr && state->window != nullptr) {
        gtk_window_minimize(state->window);
    }
}

void shell_maximize_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr) return;

    if (gtk_window_is_maximized(state->window)) {
        gtk_window_unmaximize(state->window);
    } else {
        gtk_window_maximize(state->window);
    }
    shell_update_maximize_button(state);
}

void shell_close_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state != nullptr && state->window != nullptr) {
        gtk_window_close(state->window);
    }
}

void shell_global_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->search_syncing) return;

    const char *entry_text = gtk_editable_get_text(GTK_EDITABLE(entry));
    const std::string query = entry_text == nullptr ? "" : entry_text;
    set_app_search_query(state, query);

    if (!query.empty()) {
        const char *visible =
            state->stack == nullptr
                ? nullptr
                : gtk_stack_get_visible_child_name(state->stack);
        if (visible == nullptr || std::strcmp(visible, "discover") != 0) {
            select_app_page(state, AppPage::discover);
            gtk_widget_grab_focus(GTK_WIDGET(entry));
            gtk_editable_set_position(GTK_EDITABLE(entry), -1);
        }
    }
}

GtkWidget *shell_make_header_bar(WindowState *state)
{
    GtkWidget *bar = gtk_header_bar_new();
    gtk_widget_add_css_class(bar, "infiltrator-titlebar");
    gtk_widget_add_css_class(bar, "shell-header");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(bar), false);

    GtkWidget *empty_title = gtk_label_new("");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(bar), empty_title);

    GtkWidget *brand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(brand, "titlebar-brand");
    GtkWidget *brand_icon_wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(brand_icon_wrap, "titlebar-brand-icon");

    GtkWidget *brand_logo = shell_embedded_picture(
        kTitlebarLogoBase64,
        "titlebar-logo",
        GTK_CONTENT_FIT_CONTAIN);
    if (brand_logo != nullptr) {
        gtk_widget_set_size_request(brand_logo, 48, 32);
        gtk_widget_set_valign(brand_logo, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(brand_icon_wrap), brand_logo);
    } else {
        GtkWidget *brand_icon =
            infiltrator::software::make_icon(
                "net.ssmith.infiltrator.software", 28);
        gtk_widget_add_css_class(brand_icon, "titlebar-logo-fallback");
        gtk_box_append(GTK_BOX(brand_icon_wrap), brand_icon);
    }
    gtk_box_append(GTK_BOX(brand), brand_icon_wrap);

    GtkWidget *brand_copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(
        GTK_BOX(brand_copy),
        infiltrator::software::make_label("Software", "titlebar-title"));
    gtk_box_append(
        GTK_BOX(brand_copy),
        infiltrator::software::make_label(
            "Infiltrator OS", "titlebar-subtitle"));
    gtk_box_append(GTK_BOX(brand), brand_copy);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), brand);

    GtkWidget *header_end = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(header_end, "titlebar-header-end");

    state->global_search = gtk_search_entry_new();
    gtk_widget_add_css_class(state->global_search, "global-search");
    gtk_search_entry_set_placeholder_text(
        GTK_SEARCH_ENTRY(state->global_search),
        "Search for software, applications, and packages…");
    gtk_widget_set_size_request(state->global_search, 200, -1);
    g_signal_connect(
        state->global_search,
        "search-changed",
        G_CALLBACK(shell_global_search_changed),
        state);

    GtkWidget *minimize =
        gtk_button_new_from_icon_name("window-minimize-symbolic");
    gtk_widget_add_css_class(minimize, "window-control");
    gtk_widget_set_tooltip_text(minimize, "Minimize");
    gtk_widget_set_focusable(minimize, false);
    g_signal_connect(
        minimize, "clicked", G_CALLBACK(shell_minimize_clicked), state);

    state->maximize_button =
        gtk_button_new_from_icon_name("window-maximize-symbolic");
    gtk_widget_add_css_class(state->maximize_button, "window-control");
    gtk_widget_set_tooltip_text(state->maximize_button, "Maximize / Restore");
    gtk_widget_set_focusable(state->maximize_button, false);
    g_signal_connect(
        state->maximize_button,
        "clicked",
        G_CALLBACK(shell_maximize_clicked),
        state);

    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close, "window-control");
    gtk_widget_add_css_class(close, "window-close-control");
    gtk_widget_set_tooltip_text(close, "Close");
    gtk_widget_set_focusable(close, false);
    g_signal_connect(close, "clicked", G_CALLBACK(shell_close_clicked), state);

    gtk_box_append(GTK_BOX(header_end), state->global_search);
    gtk_box_append(GTK_BOX(header_end), minimize);
    gtk_box_append(GTK_BOX(header_end), state->maximize_button);
    gtk_box_append(GTK_BOX(header_end), close);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), header_end);

    shell_update_maximize_button(state);
    return bar;
}

GtkWidget *shell_make_status_bar()
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(bar, "statusbar");

    GtkWidget *ready =
        infiltrator::software::make_label("Ready", "statusbar-text");
    gtk_widget_set_hexpand(ready, true);
    gtk_box_append(GTK_BOX(bar), ready);

    const std::string common =
        std::string("Common ") + INFILTRATR_COMMON_VERSION;
    GtkWidget *common_label = infiltrator::software::make_label(
        common.c_str(), "statusbar-text", 1.0F);
    gtk_widget_set_margin_end(common_label, 14);
    gtk_box_append(GTK_BOX(bar), common_label);

    const std::string version =
        std::string("Version ") + INFILTRATOR_SOFTWARE_VERSION;
    gtk_box_append(
        GTK_BOX(bar),
        infiltrator::software::make_label(
            version.c_str(), "statusbar-text", 1.0F));
    return bar;
}

void shell_destroy_window_state(gpointer data)
{
    auto *state = static_cast<WindowState *>(data);
    if (state == nullptr) return;

    destroy_page_state(state);
    delete state;
}

void shell_ensure_update_indicator()
{
    gchar *program = g_find_program_in_path("infiltrator-software-tray");
    if (program == nullptr) return;
    g_free(program);

    GError *error = nullptr;
    if (!g_spawn_command_line_async("infiltrator-software-tray", &error)) {
        if (error != nullptr) {
            g_warning(
                "Unable to start software update indicator: %s",
                error->message);
            g_error_free(error);
        }
    }
}

gboolean shell_window_key_pressed(
    GtkEventControllerKey *,
    guint keyval,
    guint,
    GdkModifierType state_mask,
    gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || (state_mask & GDK_CONTROL_MASK) == 0) {
        return FALSE;
    }

    const bool kernel = keyval == GDK_KEY_k || keyval == GDK_KEY_K;
    const bool security = keyval == GDK_KEY_s || keyval == GDK_KEY_S;
    if (!kernel && !security) return FALSE;

    if (kernel) {
        show_kernel_updates(state);
    } else {
        show_security_updates(state);
    }
    return TRUE;
}

void shell_activate(GtkApplication *application, gpointer)
{
    const bool open_updates =
        g_object_get_data(
            G_OBJECT(application), "infiltrator-open-updates") != nullptr;

    GList *windows = gtk_application_get_windows(application);
    GtkWindow *existing =
        windows == nullptr ? nullptr : GTK_WINDOW(windows->data);
    if (existing != nullptr) {
        auto *state = static_cast<WindowState *>(
            g_object_get_data(
                G_OBJECT(existing), kWindowStateDataKey));
        if (open_updates && state != nullptr) {
            select_app_page(state, AppPage::updates);
        }
        gtk_window_present(existing);
        g_object_set_data(
            G_OBJECT(application), "infiltrator-open-updates", nullptr);
        return;
    }

    GtkWidget *window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Software");
    gtk_window_set_icon_name(
        GTK_WINDOW(window), "net.ssmith.infiltrator.software");
    gtk_window_set_default_size(GTK_WINDOW(window), 1440, 860);
    gtk_window_set_resizable(GTK_WINDOW(window), true);

    auto *state = new WindowState{};
    state->window = GTK_WINDOW(window);

    std::string preferences_error;
    if (!infiltrator::software::load_software_preferences(
            state->preferences, preferences_error) &&
        !preferences_error.empty()) {
        g_warning(
            "Unable to load Software preferences: %s",
            preferences_error.c_str());
    }

    g_signal_connect(
        window,
        "notify::maximized",
        G_CALLBACK(shell_window_maximized_changed),
        state);

    GtkEventController *keyboard_controller = gtk_event_controller_key_new();
    g_signal_connect(
        keyboard_controller,
        "key-pressed",
        G_CALLBACK(shell_window_key_pressed),
        state);
    gtk_widget_add_controller(window, keyboard_controller);

    state->theme.initialise();
    shell_ensure_update_indicator();

    g_object_set_data_full(
        G_OBJECT(window),
        kWindowStateDataKey,
        state,
        shell_destroy_window_state);

    gtk_window_set_titlebar(
        GTK_WINDOW(window), shell_make_header_bar(state));

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "app-shell");
    gtk_window_set_child(GTK_WINDOW(window), root);

    GtkWidget *body = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(body), true);
    gtk_widget_set_vexpand(body, true);
    gtk_box_append(GTK_BOX(root), body);

    GtkWidget *stack = gtk_stack_new();
    state->stack = GTK_STACK(stack);
    gtk_stack_set_transition_type(
        state->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(state->stack, 110U);
    gtk_widget_set_hexpand(stack, true);
    gtk_widget_set_vexpand(stack, true);

    gtk_grid_attach(
        GTK_GRID(body), make_navigation(state), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(body), stack, 1, 0, 5, 1);

    select_app_page(
        state,
        open_updates ? AppPage::updates : AppPage::discover);
    gtk_box_append(GTK_BOX(root), shell_make_status_bar());

    gtk_window_present(GTK_WINDOW(window));
    state->window_presented = true;

    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer data) -> gboolean {
            auto *idle_window = GTK_WINDOW(data);
            auto *idle_state = static_cast<WindowState *>(
                g_object_get_data(
                    G_OBJECT(idle_window), kWindowStateDataKey));
            if (idle_state != nullptr) {
                refresh_selected_page(idle_state);
            }
            return G_SOURCE_REMOVE;
        },
        g_object_ref(window),
        [](gpointer data) { g_object_unref(data); });

    g_object_set_data(
        G_OBJECT(application), "infiltrator-open-updates", nullptr);
}

int shell_command_line(
    GApplication *application,
    GApplicationCommandLine *command_line,
    gpointer)
{
    GVariantDict *options =
        g_application_command_line_get_options_dict(command_line);
    const bool open_updates =
        options != nullptr && g_variant_dict_contains(options, "updates");

    g_object_set_data(
        G_OBJECT(application),
        "infiltrator-open-updates",
        open_updates ? GINT_TO_POINTER(1) : nullptr);
    g_application_activate(application);
    return 0;
}

} // namespace
} // namespace infiltrator::software::app

int main(int argc, char **argv)
{
    GtkApplication *application = gtk_application_new(
        "net.ssmith.infiltrator.software",
        G_APPLICATION_HANDLES_COMMAND_LINE);

    static const GOptionEntry options[] = {
        {
            "updates",
            0,
            0,
            G_OPTION_ARG_NONE,
            nullptr,
            "Open the Updates page",
            nullptr
        },
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr}
    };
    g_application_add_main_option_entries(
        G_APPLICATION(application), options);

    g_signal_connect(
        application,
        "activate",
        G_CALLBACK(infiltrator::software::app::shell_activate),
        nullptr);
    g_signal_connect(
        application,
        "command-line",
        G_CALLBACK(infiltrator::software::app::shell_command_line),
        nullptr);

    const int status =
        g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
