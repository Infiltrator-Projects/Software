// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_APP_SHELL_CONTRACT_HPP
#define INFILTRATOR_SOFTWARE_APP_SHELL_CONTRACT_HPP

namespace infiltrator::software::app {

struct WindowState;

/*
 * Transitional contract for the few page refresh operations still implemented
 * by the legacy app shell. Only AppCoordinator should depend on this boundary;
 * views use their dedicated services and report cross-page consequences to the
 * coordinator instead of reaching back into main.cpp.
 */
void refresh_updates(WindowState *state, bool refresh_metadata);
void refresh_discover(WindowState *state, bool force_refresh);
void rebuild_discover_repository_preview(WindowState *state);

} // namespace infiltrator::software::app

#endif
