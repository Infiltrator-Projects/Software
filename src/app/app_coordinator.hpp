// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_APP_COORDINATOR_HPP
#define INFILTRATOR_SOFTWARE_APP_COORDINATOR_HPP

namespace infiltrator::software::app {

struct WindowState;

enum class AppPage {
    discover = 0,
    installed = 1,
    updates = 2,
    system = 3,
    repositories = 4,
    history = 5,
    repair = 6
};

/*
 * Pages report domain events here instead of knowing which other pages need
 * refreshes. This is deliberately small: it is the one place where current
 * page dependencies are coordinated during the 0.3 -> 0.4 migration.
 */
void notify_kernel_state_changed(WindowState *state);
void notify_repository_state_changed(WindowState *state);
void notify_repository_snapshot_changed(WindowState *state);
void notify_history_state_changed(WindowState *state);
void notify_repair_state_changed(WindowState *state);
void select_app_page(WindowState *state, AppPage page);

} // namespace infiltrator::software::app

#endif
