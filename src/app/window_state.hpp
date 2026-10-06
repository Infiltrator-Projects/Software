// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_WINDOW_STATE_HPP
#define INFILTRATOR_SOFTWARE_WINDOW_STATE_HPP

#include "app/discover_state.hpp"
#include "app/history_controller.hpp"
#include "app/installed_controller.hpp"
#include "app/repair_view.hpp"
#include "app/repository_controller.hpp"
#include "app/system_view.hpp"
#include "app/theme.hpp"
#include "app/updates_controller.hpp"
#include "core/update_policy.hpp"

#include <gtk/gtk.h>

namespace infiltrator::software::app {

/*
 * Shell-owned state only. Page-local mutable state lives with the page or
 * controller that owns it. WindowState still inherits those page-state types
 * during the 0.3 -> 0.4 migration so legacy call sites can be moved in small,
 * reviewable steps without changing behaviour.
 */
struct ShellState {
    GtkWindow *window{};
    GtkStack *stack{};
    ThemeController theme;
    GtkWidget *maximize_button{};
    GtkWidget *global_search{};
    bool search_syncing{false};
    GtkListBox *navigation_list{};
    GtkWidget *nav_updates_badge{};
    bool window_presented{false};
};

struct WindowState final :
    ShellState,
    DiscoverPageState,
    UpdatesController,
    SystemPageState,
    RepairPageState {
    InstalledController installed;
    RepositoryController repositories;
    HistoryController history;
    SoftwarePreferences preferences{};
};

} // namespace infiltrator::software::app

#endif
