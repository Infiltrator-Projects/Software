// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_APP_COORDINATOR_HPP
#define INFILTRATOR_SOFTWARE_APP_COORDINATOR_HPP

#include "app/app_page.hpp"

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
void notify_repair_state_changed(WindowState *state);
void select_app_page(WindowState *state, AppPage page);

} // namespace infiltrator::software::app

#endif
