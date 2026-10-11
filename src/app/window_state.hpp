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

/*
 * WindowState is the shell composition root. Page/controller state is owned
 * exactly once by the named members below. It deliberately does not re-export
 * page widgets, vectors, timers, generations, or loaded flags: callers use the
 * page/controller owner instead. Do not add new aliases here.
 *
 * The single system_critical compatibility reference remains because the
 * quarantined legacy provider's spelling collides with
 * PackageRecord::system_critical when represented by the old macro bridge.
 * It disappears when that final legacy call site is decomposed.
 */
struct WindowState final : ShellState {
    DiscoverPageState discover;
    UpdatesController updates;
    SystemPageState system;
    RepairPageState repair;
    InstalledController installed;
    RepositoryController repositories;
    HistoryController history;
    SoftwarePreferences preferences{};

    GtkWidget *&system_critical = system.system_critical;
};

} // namespace infiltrator::software::app

/*
 * legacy_page_impl.cpp is compiled with a source-local main=... definition to
 * suppress its obsolete entry point. Piggy-back on that existing translation-
 * unit marker so the compatibility names are visible nowhere else. New code
 * must never depend on legacy_page_aliases.hpp.
 */
#ifdef main
#include "app/legacy_page_aliases.hpp"
#endif

#endif
