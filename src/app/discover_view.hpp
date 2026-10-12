// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DISCOVER_VIEW_HPP
#define INFILTRATOR_SOFTWARE_DISCOVER_VIEW_HPP

#include <gtk/gtk.h>
#include <string_view>

namespace infiltrator::software::app {

struct WindowState;

GtkWidget *make_discover_page(WindowState *state);
void refresh_discover(WindowState *state, bool force_refresh = false);
void rebuild_discover_repository_preview(WindowState *state);
void rebuild_discover_activity_preview(WindowState *state);
void rebuild_discover_update_preview(WindowState *state);
void set_discover_search_query(WindowState *state, std::string_view query);
void set_discover_health_state(WindowState *state, bool healthy);
void destroy_discover_page(WindowState *state);

} // namespace infiltrator::software::app

#endif
