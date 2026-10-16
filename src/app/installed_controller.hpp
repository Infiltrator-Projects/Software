// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTWARE_INSTALLED_CONTROLLER_HPP
#define SOFTWARE_INSTALLED_CONTROLLER_HPP

#include <gtk/gtk.h>

namespace infiltrator::software {

struct InstalledController {
    GtkWindow *window{};
    GtkStringList *strings{};
    GtkWidget *status{};
    GtkWidget *count{};
    GtkWidget *backend{};
    GtkWidget *backend_state{};
    unsigned int generation{0U};
    bool busy{false};
    bool loaded{false};
};

inline bool installed_controller_loaded(
    const InstalledController &controller) noexcept
{
    return controller.loaded;
}

GtkWidget *create_installed_page(
    InstalledController *controller,
    GtkWindow *window);

void refresh_installed_controller(
    InstalledController *controller);

void destroy_installed_controller(
    InstalledController *controller) noexcept;

} // namespace infiltrator::software

#endif
