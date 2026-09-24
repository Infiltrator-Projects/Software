// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/theme.hpp"

#include <infiltratr/config.h>
#include <infiltratr/posix.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace infiltrator::software {
namespace {

const char *mode_value(const InfiltratrThemeMode mode) noexcept
{
    switch (mode) {
    case INFILTRATR_THEME_DAY:
        return "day";
    case INFILTRATR_THEME_NIGHT:
        return "night";
    case INFILTRATR_THEME_SYSTEM:
    default:
        return "system";
    }
}

InfiltratrThemeMode parse_mode(
    const char *value, const InfiltratrThemeMode fallback) noexcept
{
    if (value == nullptr) {
        return fallback;
    }
    if (std::strcmp(value, "system") == 0) {
        return INFILTRATR_THEME_SYSTEM;
    }
    if (std::strcmp(value, "day") == 0) {
        return INFILTRATR_THEME_DAY;
    }
    if (std::strcmp(value, "night") == 0) {
        return INFILTRATR_THEME_NIGHT;
    }
    return fallback;
}

std::string colour(const std::uint32_t rgb)
{
    std::ostringstream stream;
    stream << '#' << std::hex << std::setfill('0') << std::setw(6)
           << (rgb & 0xFFFFFFU);
    return stream.str();
}

} // namespace

ThemeController::~ThemeController()
{
    if (settings_ != nullptr) {
        if (theme_name_handler_ != 0) {
            g_signal_handler_disconnect(settings_, theme_name_handler_);
        }
        if (prefer_dark_handler_ != 0) {
            g_signal_handler_disconnect(settings_, prefer_dark_handler_);
        }
    }

    if (provider_ != nullptr) {
        GdkDisplay *display = gdk_display_get_default();
        if (display != nullptr) {
            gtk_style_context_remove_provider_for_display(
                display, GTK_STYLE_PROVIDER(provider_));
        }
        g_object_unref(provider_);
    }
}

void ThemeController::initialise()
{
    load_preferences();

    provider_ = gtk_css_provider_new();
    GdkDisplay *display = gdk_display_get_default();
    if (display != nullptr) {
        gtk_style_context_add_provider_for_display(
            display, GTK_STYLE_PROVIDER(provider_),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 50U);
    }

    settings_ = gtk_settings_get_default();
    if (settings_ != nullptr) {
        theme_name_handler_ = g_signal_connect(
            settings_, "notify::gtk-theme-name",
            G_CALLBACK(on_system_theme_changed), this);
        prefer_dark_handler_ = g_signal_connect(
            settings_, "notify::gtk-application-prefer-dark-theme",
            G_CALLBACK(on_system_theme_changed), this);
    }

    apply();
}

GtkWidget *ThemeController::create_selector()
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(box, "appearance-selector");
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_bottom(box, 16);

    GtkWidget *label = gtk_label_new("Appearance");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_widget_add_css_class(label, "appearance-title");
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *system = gtk_check_button_new_with_label("Follow OS");
    GtkWidget *day = gtk_check_button_new_with_label("Day");
    GtkWidget *night = gtk_check_button_new_with_label("Night");

    gtk_check_button_set_group(
        GTK_CHECK_BUTTON(day), GTK_CHECK_BUTTON(system));
    gtk_check_button_set_group(
        GTK_CHECK_BUTTON(night), GTK_CHECK_BUTTON(system));

    g_object_set_data(
        G_OBJECT(system), "infiltrator-theme-mode",
        GINT_TO_POINTER(static_cast<int>(INFILTRATR_THEME_SYSTEM)));
    g_object_set_data(
        G_OBJECT(day), "infiltrator-theme-mode",
        GINT_TO_POINTER(static_cast<int>(INFILTRATR_THEME_DAY)));
    g_object_set_data(
        G_OBJECT(night), "infiltrator-theme-mode",
        GINT_TO_POINTER(static_cast<int>(INFILTRATR_THEME_NIGHT)));

    switch (mode_) {
    case INFILTRATR_THEME_DAY:
        gtk_check_button_set_active(GTK_CHECK_BUTTON(day), true);
        break;
    case INFILTRATR_THEME_NIGHT:
        gtk_check_button_set_active(GTK_CHECK_BUTTON(night), true);
        break;
    case INFILTRATR_THEME_SYSTEM:
    default:
        gtk_check_button_set_active(GTK_CHECK_BUTTON(system), true);
        break;
    }

    g_signal_connect(
        system, "toggled", G_CALLBACK(on_mode_toggled), this);
    g_signal_connect(
        day, "toggled", G_CALLBACK(on_mode_toggled), this);
    g_signal_connect(
        night, "toggled", G_CALLBACK(on_mode_toggled), this);

    gtk_box_append(GTK_BOX(box), system);
    gtk_box_append(GTK_BOX(box), day);
    gtk_box_append(GTK_BOX(box), night);
    return box;
}

InfiltratrThemeMode ThemeController::mode() const noexcept
{
    return mode_;
}

const char *ThemeController::mode_name() const noexcept
{
    return infiltratr_theme_mode_name(mode_);
}

void ThemeController::cycle_mode()
{
    set_mode(infiltratr_theme_mode_next(mode_), true);
}

void ThemeController::on_system_theme_changed(
    GtkSettings *, GParamSpec *, gpointer user_data)
{
    auto *controller = static_cast<ThemeController *>(user_data);
    if (controller != nullptr &&
        controller->mode_ == INFILTRATR_THEME_SYSTEM) {
        controller->apply();
    }
}

void ThemeController::on_mode_toggled(
    GtkCheckButton *button, gpointer user_data)
{
    if (!gtk_check_button_get_active(button)) {
        return;
    }

    auto *controller = static_cast<ThemeController *>(user_data);
    if (controller == nullptr) {
        return;
    }

    const int raw = GPOINTER_TO_INT(
        g_object_get_data(
            G_OBJECT(button), "infiltrator-theme-mode"));
    if (raw < static_cast<int>(INFILTRATR_THEME_SYSTEM) ||
        raw > static_cast<int>(INFILTRATR_THEME_NIGHT)) {
        return;
    }

    controller->set_mode(
        static_cast<InfiltratrThemeMode>(raw), true);
}

bool ThemeController::write_preferences(
    FILE *stream, const void *user_data)
{
    const auto *controller =
        static_cast<const ThemeController *>(user_data);
    return controller != nullptr &&
           std::fprintf(
               stream,
               "# Infiltrator Software graphical preferences\n"
               "theme_mode=%s\n",
               mode_value(controller->mode_)) >= 0 &&
           std::ferror(stream) == 0;
}

bool ThemeController::system_prefers_dark() const
{
    if (settings_ == nullptr) {
        return false;
    }

    gboolean prefer_dark = FALSE;
    gchar *theme_name = nullptr;
    g_object_get(
        settings_,
        "gtk-application-prefer-dark-theme", &prefer_dark,
        "gtk-theme-name", &theme_name,
        nullptr);

    bool dark = prefer_dark != FALSE;
    if (theme_name != nullptr) {
        gchar *lower = g_ascii_strdown(theme_name, -1);
        dark = dark ||
               (lower != nullptr &&
                std::strstr(lower, "dark") != nullptr);
        g_free(lower);
        g_free(theme_name);
    }
    return dark;
}

void ThemeController::apply()
{
    if (provider_ == nullptr) {
        return;
    }

    const InfiltratrThemePalette *palette =
        infiltratr_theme_resolve(mode_, system_prefers_dark());
    const InfiltratrTypography *typography = infiltratr_typography();
    const InfiltratrDesignMetrics *metrics = infiltratr_design_metrics();
    if (palette == nullptr || typography == nullptr ||
        metrics == nullptr || typography->ui_family == nullptr ||
        typography->brand_family == nullptr) {
        return;
    }

    const std::string background = colour(palette->background_rgb);
    const std::string panel = colour(palette->panel_rgb);
    const std::string card = colour(palette->card_rgb);
    const std::string surface = colour(palette->surface_rgb);
    const std::string border = colour(palette->border_rgb);
    const std::string text = colour(palette->text_rgb);
    const std::string title = colour(palette->title_rgb);
    const std::string button_bg = colour(palette->button_background_rgb);
    const std::string button_fg = colour(palette->button_foreground_rgb);
    const std::string select_bg = colour(palette->selection_background_rgb);
    const std::string select_fg = colour(palette->selection_foreground_rgb);
    const std::string neutral = colour(palette->neutral_accent_rgb);
    const std::string success = colour(palette->success_rgb);
    const std::string warning = colour(palette->warning_rgb);
    const std::string fault = colour(palette->fault_rgb);
    const std::string info = colour(palette->info_rgb);
    const std::string operation = colour(palette->operation_rgb);
    const std::string card_hover = colour(palette->card_hover_rgb);
    const std::string operation_hover = colour(palette->operation_hover_rgb);
    const std::string titlebar = colour(palette->titlebar_rgb);
    const std::string connection = colour(palette->connection_rgb);
    const std::string connection_border = colour(palette->connection_border_rgb);
    const std::string heading = colour(palette->heading_rgb);
    const std::string summary = colour(palette->summary_rgb);
    const std::string kicker = colour(palette->kicker_rgb);
    const std::string detail_label = colour(palette->detail_label_rgb);
    const std::string note = colour(palette->note_rgb);
    const std::string status_border = colour(palette->status_border_rgb);
    const std::string accent_fg = colour(palette->accent_foreground_rgb);
    const std::string accent_hover = colour(palette->accent_hover_rgb);
    const std::string selected_summary = colour(palette->selected_summary_rgb);
    const std::string warning_muted = colour(palette->warning_muted_rgb);
    const std::string warning_border = colour(palette->warning_border_rgb);
    const std::string success_border = colour(palette->success_border_rgb);

    std::ostringstream css;
    css
        << "* { font-family: \"" << typography->ui_family
        << "\"; font-weight: " << typography->ui_regular_weight << "; }"
        << "window, .background { background: " << background
        << "; color: " << text << "; }"

        << "headerbar.infiltrator-titlebar { min-height: 44px; background: "
        << titlebar << "; color: " << title
        << "; border-bottom: 1px solid " << border << "; padding: 0 6px; }"
        << ".titlebar-title { font-family: \"" << typography->brand_family
        << "\"; font-size: 18px; font-weight: " << typography->ui_bold_weight
        << "; color: " << title << "; }"
        << ".titlebar-subtitle { color: " << summary
        << "; font-size: 12px; font-weight: " << typography->ui_bold_weight
        << "; }"
        << "headerbar button { min-height: 30px; padding: 0 12px; background: "
        << button_bg << "; border: 1px solid " << border
        << "; border-radius: " << metrics->small_radius
        << "px; font-weight: " << typography->ui_bold_weight << "; }"
        << "headerbar button, headerbar button label, headerbar button image { color: "
        << button_fg << "; opacity: 1; }"
        << "headerbar button:hover { background: " << neutral << "; }"
        << "headerbar button:hover, headerbar button:hover label, "
        << "headerbar button:hover image { color: " << button_fg << "; opacity: 1; }"
        << "headerbar button:active, headerbar button:checked { background: "
        << select_bg << "; border-color: " << operation << "; }"
        << "headerbar button:active, headerbar button:active label, "
        << "headerbar button:active image, headerbar button:checked, "
        << "headerbar button:checked label, headerbar button:checked image { color: "
        << select_fg << "; opacity: 1; }"
        << ".titlebar-button { margin: 3px 2px; }"

        << ".sidebar { background: " << panel
        << "; border-right: 1px solid " << border << "; }"
        << ".sidebar-title { font-size: 11px; font-weight: "
        << typography->ui_bold_weight << "; color: " << kicker << "; }"
        << ".nav-list { background: transparent; }"
        << ".nav-row { margin: 3px 8px; padding: 11px 12px; border: 1px solid transparent; "
        << "border-radius: " << metrics->small_radius << "px; }"
        << ".nav-row:hover { background: " << card_hover << "; }"
        << ".nav-row:selected { background: " << select_bg
        << "; border-color: " << border << "; border-left-width: 3px; "
        << "border-left-color: " << operation << "; }"
        << ".nav-label { font-size: 14px; font-weight: "
        << typography->ui_bold_weight << "; color: " << text << "; }"
        << ".nav-row:selected .nav-label { color: " << select_fg << "; }"
        << ".nav-row image { color: " << summary << "; }"
        << ".nav-discover image { color: " << info << "; }"
        << ".nav-installed image { color: " << success << "; }"
        << ".nav-updates image { color: " << warning << "; }"
        << ".nav-system image { color: " << heading << "; }"
        << ".nav-repositories image { color: " << info << "; opacity: 1; }"
        << ".nav-history image { color: " << info << "; }"
        << ".nav-repair image { color: " << fault << "; }"
        << ".nav-row:selected image { color: " << select_fg << "; }"
        << ".sidebar-footer { padding: 12px 16px; border-top: 1px dashed "
        << border << "; }"
        << ".sidebar-note { font-size: 11px; color: " << summary << "; }"

        << ".content { padding: 30px 34px 24px 34px; background: "
        << background << "; }"
        << ".page-hero { margin-bottom: 2px; }"
        << ".page-icon { min-width: 44px; min-height: 44px; border-radius: "
        << metrics->control_radius << "px; background: " << surface
        << "; border: 1px solid " << status_border << "; }"
        << ".page-icon image { color: " << info << "; }"
        << ".hero-title { font-family: \"" << typography->brand_family
        << "\"; font-size: 28px; font-weight: " << typography->brand_weight
        << "; color: " << heading << "; }"
        << ".hero-subtitle { font-size: 12px; color: " << summary << "; }"

        << ".page-discover .page-icon, .page-discover .page-icon image, "
        << ".page-discover .hero-title { color: " << info << "; border-color: " << info << "; }"
        << ".page-installed .page-icon, .page-installed .page-icon image, "
        << ".page-installed .hero-title { color: " << success << "; border-color: " << success_border << "; }"
        << ".page-updates .page-icon, .page-updates .page-icon image, "
        << ".page-updates .hero-title { color: " << warning << "; border-color: " << warning_border << "; }"
        << ".page-system .page-icon, .page-system .page-icon image, "
        << ".page-system .hero-title { color: " << heading << "; border-color: " << status_border << "; }"
        << ".page-repositories .page-icon, .page-repositories .page-icon image, "
        << ".page-repositories .hero-title { color: " << info << "; border-color: " << info << "; }"
        << ".page-history .page-icon, .page-history .page-icon image, "
        << ".page-history .hero-title { color: " << info << "; border-color: " << info << "; }"
        << ".page-repair .page-icon, .page-repair .page-icon image, "
        << ".page-repair .hero-title { color: " << fault << "; border-color: " << fault << "; }"

        << ".stat-card { padding: 15px 16px; border-radius: "
        << metrics->control_radius << "px; background: " << card
        << "; border: 1px solid " << border << "; }"
        << ".stat-caption, .kicker { font-size: 10px; font-weight: "
        << typography->ui_bold_weight << "; color: " << kicker << "; }"
        << ".stat-value { font-size: 17px; font-weight: "
        << typography->ui_bold_weight << "; color: " << heading << "; }"
        << ".stat-info { border-color: " << info << "; }"
        << ".stat-info .stat-value { color: " << info << "; }"
        << ".stat-operation { border-color: " << operation << "; }"
        << ".stat-operation .stat-value { color: " << operation << "; }"
        << ".stat-success { border-color: " << success_border << "; }"
        << ".stat-success .stat-value { color: " << success << "; }"

        << ".card { padding: 18px; border-radius: " << metrics->control_radius
        << "px; background: " << card << "; border: 1px solid " << border << "; }"
        << ".card-info { border-color: " << info << "; }"
        << ".page-discover .card { border-color: " << info << "; }"
        << ".page-updates .card { border-color: " << warning_border << "; }"
        << ".page-system .card { border-color: " << status_border << "; }"
        << ".page-repositories .card { border-color: " << operation << "; }"
        << ".page-history .card { border-color: " << info << "; }"
        << ".page-repair .card { border-color: " << fault << "; }"
        << ".card-title { font-family: \"" << typography->brand_family
        << "\"; font-size: 16px; font-weight: " << typography->ui_bold_weight
        << "; color: " << heading << "; }"
        << ".card-copy { font-size: 12px; color: " << note << "; }"
        << ".page-updates .kicker { color: " << warning << "; }"
        << ".page-repositories .kicker { color: " << operation << "; }"
        << ".page-repair .kicker { color: " << fault << "; }"

        << ".package-list, listview, scrolledwindow { background: " << card << "; }"
        << ".package-list { border: 1px solid " << status_border
        << "; border-radius: " << metrics->control_radius << "px; }"
        << ".package-row { padding: 10px 12px; }"
        << ".package-row label { font-size: 13px; color: " << text << "; }"
        << "image.package-icon { color: " << operation << "; opacity: 1; }"
        << "listview row { border-bottom: 1px solid " << border << "; }"
        << "listview row:hover { background: " << card_hover << "; }"
        << "listview row:selected { background: " << select_bg << "; }"
        << "listview row:selected label, listview row:selected image { color: "
        << select_fg << "; }"

        << ".discover-controls { padding: 10px 12px; border: 1px solid "
        << status_border << "; border-radius: " << metrics->control_radius
        << "px; background: " << surface << "; }"
        << ".discover-controls entry, .discover-controls dropdown { min-height: 34px; }"
        << ".discover-status { font-size: 11px; color: " << summary << "; }"
        << ".discover-card { padding: 16px; background: " << card
        << "; border: 1px solid " << border << "; border-radius: "
        << metrics->card_radius << "px; }"
        << ".discover-card:hover { background: " << card_hover
        << "; border-color: " << info << "; }"
        << ".discover-app-icon { color: " << operation << "; }"
        << ".discover-name { font-family: \"" << typography->brand_family
        << "\"; font-size: 18px; font-weight: " << typography->brand_weight
        << "; color: " << heading << "; }"
        << ".discover-meta { font-size: 11px; font-weight: "
        << typography->ui_bold_weight << "; color: " << kicker << "; }"
        << ".discover-description { font-size: 12px; color: " << note << "; }"
        << ".state-installed, .state-available { padding: 4px 9px; border-radius: 999px; "
        << "font-size: 11px; font-weight: " << typography->ui_bold_weight << "; }"
        << ".state-installed { color: " << success << "; border: 1px solid "
        << success_border << "; background: " << surface << "; }"
        << ".state-available { color: " << info << "; border: 1px solid "
        << info << "; background: " << surface << "; }"
        << ".source-card { padding: 14px; background: " << card
        << "; border: 1px solid " << operation << "; border-radius: "
        << metrics->card_radius << "px; }"
        << ".source-card:hover { background: " << card_hover << "; }"
        << ".source-icon { color: " << operation << "; }"
        << ".source-name { font-family: \"" << typography->brand_family
        << "\"; font-size: 16px; font-weight: "
        << typography->ui_bold_weight << "; color: " << heading << "; }"
        << ".source-meta { font-size: 11px; font-weight: "
        << typography->ui_bold_weight << "; color: " << kicker << "; }"
        << ".source-location { font-size: 12px; color: " << text << "; }"
        << ".source-detail { font-size: 11px; color: " << note << "; }"
        << ".source-file { font-size: 10px; color: " << summary << "; }"
        /*
         * Repository state controls are GtkButtons. GTK's generic
         * "button label" rule sets the child label colour explicitly, so the
         * state colour on the button itself does not inherit into its label.
         * Style both the button and its child label to keep Enabled/Disabled
         * readable in every palette.
         */
        << "button.source-state-toggle { min-height: 30px; padding: 0 11px; "
        << "border-radius: 999px; background: " << surface << "; }"
        << "button.source-state-toggle.state-installed { border-color: "
        << success_border << "; background: " << surface << "; }"
        << "button.source-state-toggle.state-installed, "
        << "button.source-state-toggle.state-installed label { color: "
        << success << "; opacity: 1; }"
        << "button.source-state-toggle.state-available { border-color: "
        << info << "; background: " << surface << "; }"
        << "button.source-state-toggle.state-available, "
        << "button.source-state-toggle.state-available label { color: "
        << info << "; opacity: 1; }"
        << "button.source-state-toggle.state-installed:hover { background: "
        << card_hover << "; border-color: " << success << "; }"
        << "button.source-state-toggle.state-installed:hover, "
        << "button.source-state-toggle.state-installed:hover label { color: "
        << success << "; opacity: 1; }"
        << "button.source-state-toggle.state-available:hover { background: "
        << card_hover << "; border-color: " << info << "; }"
        << "button.source-state-toggle.state-available:hover, "
        << "button.source-state-toggle.state-available:hover label { color: "
        << info << "; opacity: 1; }"
        << ".discover-details { min-height: 30px; }"
        << ".detail-page { background: " << background << "; }"
        << ".detail-hero { padding-bottom: 4px; }"
        << ".detail-label { min-width: 130px; font-size: 11px; color: "
        << detail_label << "; }"
        << ".detail-value { font-size: 12px; font-weight: "
        << typography->ui_bold_weight << "; color: " << text << "; }"
        << ".detail-note { padding: 10px 12px; border-left: 3px solid "
        << warning << "; background: " << surface << "; color: "
        << warning_muted << "; font-size: 11px; }"

        << ".statusbar { padding: 8px 12px; border-top: 1px solid "
        << connection_border << "; background: " << connection << "; }"
        << ".statusbar-text { font-size: 11px; color: " << summary << "; }"

        << "button { min-height: 30px; padding: 0 12px; background: "
        << button_bg << "; border: 1px solid " << border
        << "; border-radius: " << metrics->small_radius
        << "px; font-weight: " << typography->ui_bold_weight << "; }"
        << "button, button label, button image { color: " << button_fg << "; }"
        << "button:hover { background: " << operation_hover << "; }"
        << "button:hover, button:hover label, button:hover image { color: " << text << "; }"
        << "button.suggested-action { background: " << operation
        << "; border-color: " << operation << "; }"
        << "button.suggested-action, button.suggested-action label, "
        << "button.suggested-action image { color: " << accent_fg << "; }"
        << "button.suggested-action:hover { background: " << accent_hover
        << "; border-color: " << accent_hover << "; }"
        << "selection { background: " << select_bg << "; color: " << select_fg << "; }"
        << ".selected-summary { color: " << selected_summary << "; }"
        << ".warning-muted { color: " << warning_muted << "; }"
        << ".detail-label { color: " << detail_label << "; }";

    const std::string css_text = css.str();
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider_, css_text.c_str());
#else
    gtk_css_provider_load_from_data(
        provider_, css_text.c_str(),
        static_cast<gssize>(css_text.size()));
#endif
}

void ThemeController::load_preferences()
{
    const std::string path = preferences_path();
    FILE *file = std::fopen(path.c_str(), "r");
    if (file == nullptr) {
        return;
    }

    char line[256];
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        char *key = nullptr;
        char *value = nullptr;
        if (infiltratr_config_parse_line(
                line, &key, &value) !=
            INFILTRATR_CONFIG_LINE_ENTRY) {
            continue;
        }

        if (std::strcmp(key, "theme_mode") == 0) {
            mode_ = parse_mode(value, mode_);
        }
    }

    std::fclose(file);
}

void ThemeController::save_preferences() const
{
    const std::string path = preferences_path();
    gchar *directory = g_path_get_dirname(path.c_str());
    if (directory == nullptr) {
        return;
    }

    const int directory_result =
        g_mkdir_with_parents(directory, 0700);
    g_free(directory);
    if (directory_result != 0) {
        return;
    }

    (void)infiltratr_atomic_file_write(
        path.c_str(), INFILTRATR_ATOMIC_FILE_PRIVATE,
        write_preferences, this);
}

void ThemeController::set_mode(
    const InfiltratrThemeMode mode, const bool persist)
{
    if (mode < INFILTRATR_THEME_SYSTEM ||
        mode > INFILTRATR_THEME_NIGHT) {
        return;
    }

    mode_ = mode;
    apply();
    if (persist) {
        save_preferences();
    }
}

std::string ThemeController::preferences_path() const
{
    gchar *path = g_build_filename(
        g_get_user_config_dir(),
        "infiltrator", "software", "preferences.conf",
        nullptr);
    if (path == nullptr) {
        return {};
    }

    std::string result(path);
    g_free(path);
    return result;
}

} // namespace infiltrator::software
