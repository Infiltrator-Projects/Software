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
 * WindowState is only the composition root for the native shell and its page
 * owners. It deliberately does not re-export page widget/vector/timer state.
 * New code must use the named owner. During the last main.cpp decomposition
 * the compatibility spellings below are preprocessor-only and must shrink;
 * do not add new aliases. They create no reference members or duplicate state
 * and disappear once Discover and Updates leave the legacy translation unit.
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
 * main.cpp includes kernel_manager.hpp before this header; the extracted
 * controller/view units do not. Keep the final source-compatibility bridge
 * confined to that one legacy translation unit so owner-qualified code cannot
 * be rewritten by these macros. The bridge is deleted with the main split.
 */
#ifdef INFILTRATOR_SOFTWARE_KERNEL_MANAGER_HPP
#define discover_visible discover.discover_visible
#define discover_search discover.discover_search
#define discover_category discover.discover_category
#define discover_categories discover.discover_categories
#define discover_status discover.discover_status
#define discover_count discover.discover_count
#define discover_source discover.discover_source
#define discover_state discover.discover_state
#define discover_featured_flow discover.discover_featured_flow
#define discover_update_preview discover.discover_update_preview
#define discover_repository_preview discover.discover_repository_preview
#define discover_activity_preview discover.discover_activity_preview
#define discover_health_banner_state discover.discover_health_banner_state
#define discover_updates_summary discover.discover_updates_summary
#define discover_health_summary discover.discover_health_summary
#define discover_repositories_summary discover.discover_repositories_summary
#define discover_records discover.discover_records
#define discover_search_texts discover.discover_search_texts
#define discover_generation discover.discover_generation
#define discover_loaded discover.discover_loaded

#define updates_list updates.updates_list
#define updates_status updates.updates_status
#define updates_count updates.updates_count
#define updates_critical updates.updates_critical
#define updates_install updates.updates_install
#define updates_security updates.updates_security
#define updates_refresh updates.updates_refresh
#define updates_reboot_banner updates.updates_reboot_banner
#define updates_reboot_detail updates.updates_reboot_detail
#define updates_backend updates.updates_backend
#define external_updates_list updates.external_updates_list
#define external_updates_status updates.external_updates_status
#define external_updates_spinner updates.external_updates_spinner
#define external_flatpak_apply updates.external_flatpak_apply
#define external_cinnamon_apply updates.external_cinnamon_apply
#define external_update_records updates.external_update_records
#define selected_flatpak_refs updates.selected_flatpak_refs
#define selected_cinnamon_refs updates.selected_cinnamon_refs
#define updates_transaction_panel updates.updates_transaction_panel
#define updates_transaction_phase updates.updates_transaction_phase
#define updates_transaction_detail updates.updates_transaction_detail
#define updates_transaction_meta updates.updates_transaction_meta
#define updates_stage_labels updates.updates_stage_labels
#define updates_progress updates.updates_progress
#define updates_progress_timer_id updates.updates_progress_timer_id
#define updates_progress_started_us updates.updates_progress_started_us
#define updates_progress_items updates.updates_progress_items
#define updates_progress_download_bytes updates.updates_progress_download_bytes
#define updates_progress_token updates.updates_progress_token
#define updates_progress_phase updates.updates_progress_phase
#define updates_post_install_refresh updates.updates_post_install_refresh
#define updates_restart_after_verify updates.updates_restart_after_verify
#define update_records updates.update_records
#define selected_update_ids updates.selected_update_ids
#define pending_update_selection updates.pending_update_selection
#define pending_update_plan updates.pending_update_plan
#define updates_generation updates.updates_generation
#define updates_busy updates.updates_busy
#define external_updates_active updates.external_updates_active
#define updates_auto_refresh_pending updates.updates_auto_refresh_pending
#define updates_last_metadata_refresh_us updates.updates_last_metadata_refresh_us
#define updates_refresh_timer_id updates.updates_refresh_timer_id
#define updates_loaded updates.updates_loaded

#define system_list system.system_list
#define system_status system.system_status
#define system_count system.system_count
#define system_updates system.system_updates
#define system_critical system.system_critical
#define system_refresh system.system_refresh
#define system_review_updates system.system_review_updates
#define system_records system.system_records
#define system_update_records system.system_update_records
#define system_generation system.system_generation
#define system_busy system.system_busy
#define system_loaded system.system_loaded

#define repair_list repair.repair_list
#define repair_status repair.repair_status
#define repair_engine repair.repair_engine
#define repair_sources repair.repair_sources
#define repair_issues repair.repair_issues
#define repair_health_banner repair.repair_health_banner
#define repair_health_icon repair.repair_health_icon
#define repair_health_title repair.repair_health_title
#define repair_health_copy repair.repair_health_copy
#define repair_recheck repair.repair_recheck
#define repair_rebuild repair.repair_rebuild
#define repair_configure repair.repair_configure
#define repair_generation repair.repair_generation
#define repair_busy repair.repair_busy
#define repair_interrupted repair.repair_interrupted
#define repair_loaded repair.repair_loaded
#endif

#endif
