// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTWARE_DISCOVER_VIEW_HPP
#define SOFTWARE_DISCOVER_VIEW_HPP

#include "app/view_intents.hpp"

#include <gtk/gtk.h>

namespace infiltrator::software::app {

struct WindowState;

GtkWidget *make_discover_page(WindowState *state);
void refresh_discover(WindowState *state, bool force_refresh = false);
void rebuild_discover_repository_preview(WindowState *state);
void rebuild_discover_activity_preview(WindowState *state);
void rebuild_discover_update_preview(WindowState *state);

} // namespace infiltrator::software::app

#endif
