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
 * page/controller owner instead; do not add new aliases here.
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
};

} // namespace infiltrator::software::app

/*
 * Compatibility spellings are injected only after all owner declarations are
 * complete, and only for the explicitly marked legacy translation units.
 * Ordinary includes of WindowState and page headers never expose these macros.
 */
#ifdef INFILTRATOR_SOFTWARE_REPAIR_VIEW_LEGACY_ALIASES
#include "app/repair_view_legacy_aliases.hpp"
#endif
#ifdef INFILTRATOR_SOFTWARE_LEGACY_PAGE_IMPL
#include "app/legacy_page_aliases.hpp"
#endif

#endif
