// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_UPDATES_VIEW_HPP
#define INFILTRATOR_SOFTWARE_UPDATES_VIEW_HPP

#include <gtk/gtk.h>

namespace infiltrator::software::app {

struct WindowState;

enum class UpdateSelectionPreset {
    all,
    security,
    kernel
};

GtkWidget *make_updates_page(WindowState *state);
void refresh_updates(WindowState *state, bool refresh_metadata = false);
void refresh_updates_internal(
    WindowState *state,
    bool refresh_metadata,
    bool refresh_external);
void set_updates_page_selected(WindowState *state, bool selected);
void select_update_preset(WindowState *state, UpdateSelectionPreset preset);
void destroy_updates_page(WindowState *state);

} // namespace infiltrator::software::app

#endif
