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

    std::ostringstream css;
    css
        << "* { font-family: \"" << typography->ui_family
        << "\"; color: " << colour(palette->text_rgb) << "; }"
        << "window, .background { background-color: "
        << colour(palette->background_rgb) << "; }"
        << ".sidebar { background-color: " << colour(palette->panel_rgb)
        << "; border-right: 1px solid " << colour(palette->border_rgb)
        << "; }"
        << ".nav-row { padding: " << metrics->compact_spacing << "px "
        << metrics->control_spacing << "px; border-radius: "
        << metrics->control_radius << "px; }"
        << ".nav-row:selected { background-color: "
        << colour(palette->selection_background_rgb)
        << "; color: " << colour(palette->selection_foreground_rgb)
        << "; }"
        << ".content { padding: " << metrics->content_padding << "px; }"
        << ".section-title { font-family: \"" << typography->brand_family
        << "\"; font-weight: " << typography->brand_weight
        << "; font-size: 26px; color: " << colour(palette->title_rgb)
        << "; }"
        << ".appearance-title { font-weight: "
        << typography->ui_bold_weight << "; color: "
        << colour(palette->title_rgb) << "; }"
        << ".muted { color: " << colour(palette->muted_rgb) << "; }"
        << ".package-list, listview, scrolledwindow { background-color: "
        << colour(palette->card_rgb) << "; }"
        << "button, entry, checkbutton { color: "
        << colour(palette->text_rgb) << "; }"
        << "button { background-image: none; background-color: "
        << colour(palette->button_background_rgb)
        << "; color: " << colour(palette->button_foreground_rgb)
        << "; border: 1px solid " << colour(palette->border_rgb)
        << "; border-radius: " << metrics->control_radius << "px; }"
        << "button:hover { background-color: "
        << colour(palette->card_hover_rgb) << "; }"
        << "listview row:selected { background-color: "
        << colour(palette->selection_background_rgb)
        << "; color: " << colour(palette->selection_foreground_rgb)
        << "; }"
        << "selection { background-color: "
        << colour(palette->selection_background_rgb)
        << "; color: " << colour(palette->selection_foreground_rgb)
        << "; }";

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
