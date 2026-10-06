// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_APP_SHELL_CONTRACT_HPP
#define INFILTRATOR_SOFTWARE_APP_SHELL_CONTRACT_HPP

#include <gtk/gtk.h>

namespace infiltrator::software::app {

struct WindowState;

/*
 * Transitional hooks for page work still implemented by the legacy app shell.
 * AppCoordinator and NavigationController are the only cross-page consumers;
 * this surface must shrink as Discover and Updates lifecycle moves to owners.
 */
GtkWidget *make_discover_page(WindowState *state);
GtkWidget *make_updates_page(WindowState *state);
void refresh_updates(WindowState *state, bool refresh_metadata);
void refresh_updates_internal(
    WindowState *state,
    bool refresh_metadata,
    bool refresh_external);
void refresh_discover(WindowState *state, bool force_refresh);
void rebuild_discover_repository_preview(WindowState *state);
void rebuild_discover_activity_preview(WindowState *state);

} // namespace infiltrator::software::app

#endif
