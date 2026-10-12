// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_APP_COORDINATOR_HPP
#define INFILTRATOR_SOFTWARE_APP_COORDINATOR_HPP

#include "app/app_page.hpp"

#include <string_view>

namespace infiltrator::software::app {

struct WindowState;

/*
 * Pages report domain events here instead of knowing which other pages need
 * refreshes.  This is the single cross-page policy surface; page internals are
 * never exposed through it.
 */
void notify_kernel_state_changed(WindowState *state);
void notify_repository_state_changed(WindowState *state);
void notify_repository_snapshot_changed(WindowState *state);
void notify_history_state_changed(WindowState *state);
void notify_repair_health_changed(WindowState *state, bool healthy);
void notify_repair_state_changed(WindowState *state);

/* Shell intents are translated here so the shell never reaches into pages. */
void set_app_search_query(WindowState *state, std::string_view query);
void show_security_updates(WindowState *state);
void show_kernel_updates(WindowState *state);
void select_app_page(WindowState *state, AppPage page);

} // namespace infiltrator::software::app

#endif
