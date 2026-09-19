// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_THEME_HPP
#define INFILTRATOR_SOFTWARE_THEME_HPP

#include <gtk/gtk.h>
#include <infiltratr/design.h>

#include <cstdio>
#include <string>

namespace infiltrator::software {

class ThemeController final {
public:
    ThemeController() = default;
    ~ThemeController();

    ThemeController(const ThemeController &) = delete;
    ThemeController &operator=(const ThemeController &) = delete;

    void initialise();
    [[nodiscard]] GtkWidget *create_selector();
    [[nodiscard]] InfiltratrThemeMode mode() const noexcept;

private:
    static void on_system_theme_changed(
        GtkSettings *settings, GParamSpec *pspec, gpointer user_data);
    static void on_mode_toggled(GtkCheckButton *button, gpointer user_data);
    static bool write_preferences(FILE *stream, const void *user_data);

    [[nodiscard]] bool system_prefers_dark() const;
    void apply();
    void load_preferences();
    void save_preferences() const;
    void set_mode(InfiltratrThemeMode mode, bool persist);
    [[nodiscard]] std::string preferences_path() const;

    InfiltratrThemeMode mode_{INFILTRATR_THEME_SYSTEM};
    GtkCssProvider *provider_{nullptr};
    GtkSettings *settings_{nullptr};
    gulong theme_name_handler_{0};
    gulong prefer_dark_handler_{0};
};

} // namespace infiltrator::software

#endif
