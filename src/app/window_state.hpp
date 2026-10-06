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
 * WindowState owns page state; it is no longer a page-state subtype.  The
 * reference members below are an explicit compatibility bridge for legacy
 * main.cpp call sites during the 0.3 -> 0.4 decomposition.  New code must use
 * the named owners (discover/updates/system/repair) instead.  This makes the
 * remaining coupling visible and mechanically removable instead of silently
 * exposing every page through multiple inheritance.
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

    // Legacy Discover aliases — do not add new aliases.
    GtkStringList *&discover_visible = discover.discover_visible;
    GtkWidget *&discover_search = discover.discover_search;
    GtkWidget *&discover_category = discover.discover_category;
    GtkStringList *&discover_categories = discover.discover_categories;
    GtkWidget *&discover_status = discover.discover_status;
    GtkWidget *&discover_count = discover.discover_count;
    GtkWidget *&discover_source = discover.discover_source;
    GtkWidget *&discover_state = discover.discover_state;
    GtkWidget *&discover_featured_flow = discover.discover_featured_flow;
    GtkWidget *&discover_update_preview = discover.discover_update_preview;
    GtkWidget *&discover_repository_preview = discover.discover_repository_preview;
    GtkWidget *&discover_activity_preview = discover.discover_activity_preview;
    GtkWidget *&discover_health_banner_state = discover.discover_health_banner_state;
    GtkWidget *&discover_updates_summary = discover.discover_updates_summary;
    GtkWidget *&discover_health_summary = discover.discover_health_summary;
    GtkWidget *&discover_repositories_summary = discover.discover_repositories_summary;
    std::vector<PackageRecord> &discover_records = discover.discover_records;
    std::vector<std::string> &discover_search_texts = discover.discover_search_texts;
    unsigned int &discover_generation = discover.discover_generation;
    bool &discover_loaded = discover.discover_loaded;

    // Legacy Updates aliases — do not add new aliases.
    GtkListBox *&updates_list = updates.updates_list;
    GtkWidget *&updates_status = updates.updates_status;
    GtkWidget *&updates_count = updates.updates_count;
    GtkWidget *&updates_critical = updates.updates_critical;
    GtkWidget *&updates_install = updates.updates_install;
    GtkWidget *&updates_security = updates.updates_security;
    GtkWidget *&updates_refresh = updates.updates_refresh;
    GtkWidget *&updates_reboot_banner = updates.updates_reboot_banner;
    GtkWidget *&updates_reboot_detail = updates.updates_reboot_detail;
    GtkWidget *&updates_backend = updates.updates_backend;
    GtkListBox *&external_updates_list = updates.external_updates_list;
    GtkWidget *&external_updates_status = updates.external_updates_status;
    GtkWidget *&external_updates_spinner = updates.external_updates_spinner;
    GtkWidget *&external_flatpak_apply = updates.external_flatpak_apply;
    GtkWidget *&external_cinnamon_apply = updates.external_cinnamon_apply;
    std::vector<ExternalUpdate> &external_update_records = updates.external_update_records;
    std::unordered_set<std::string> &selected_flatpak_refs = updates.selected_flatpak_refs;
    std::unordered_set<std::string> &selected_cinnamon_refs = updates.selected_cinnamon_refs;
    GtkWidget *&updates_transaction_panel = updates.updates_transaction_panel;
    GtkWidget *&updates_transaction_phase = updates.updates_transaction_phase;
    GtkWidget *&updates_transaction_detail = updates.updates_transaction_detail;
    GtkWidget *&updates_transaction_meta = updates.updates_transaction_meta;
    GtkWidget *(&updates_stage_labels)[6] = updates.updates_stage_labels;
    GtkWidget *&updates_progress = updates.updates_progress;
    guint &updates_progress_timer_id = updates.updates_progress_timer_id;
    gint64 &updates_progress_started_us = updates.updates_progress_started_us;
    std::size_t &updates_progress_items = updates.updates_progress_items;
    std::uint64_t &updates_progress_download_bytes = updates.updates_progress_download_bytes;
    std::string &updates_progress_token = updates.updates_progress_token;
    std::string &updates_progress_phase = updates.updates_progress_phase;
    bool &updates_post_install_refresh = updates.updates_post_install_refresh;
    bool &updates_restart_after_verify = updates.updates_restart_after_verify;
    std::vector<PackageRecord> &update_records = updates.update_records;
    std::unordered_set<std::string> &selected_update_ids = updates.selected_update_ids;
    std::string &pending_update_selection = updates.pending_update_selection;
    std::optional<TransactionPlan> &pending_update_plan = updates.pending_update_plan;
    unsigned int &updates_generation = updates.updates_generation;
    bool &updates_busy = updates.updates_busy;
    bool &external_updates_active = updates.external_updates_active;
    bool &updates_auto_refresh_pending = updates.updates_auto_refresh_pending;
    gint64 &updates_last_metadata_refresh_us = updates.updates_last_metadata_refresh_us;
    guint &updates_refresh_timer_id = updates.updates_refresh_timer_id;
    bool &updates_loaded = updates.updates_loaded;

    // Legacy System aliases — system_view is migrated next; main has only a
    // small number of remaining direct checks.
    GtkListBox *&system_list = system.system_list;
    GtkWidget *&system_status = system.system_status;
    GtkWidget *&system_count = system.system_count;
    GtkWidget *&system_updates = system.system_updates;
    GtkWidget *&system_critical = system.system_critical;
    GtkWidget *&system_refresh = system.system_refresh;
    GtkWidget *&system_review_updates = system.system_review_updates;
    std::vector<PackageRecord> &system_records = system.system_records;
    std::vector<PackageRecord> &system_update_records = system.system_update_records;
    unsigned int &system_generation = system.system_generation;
    bool &system_busy = system.system_busy;
    bool &system_loaded = system.system_loaded;

    // Legacy Repair aliases — repair_view is migrated next.
    GtkListBox *&repair_list = repair.repair_list;
    GtkWidget *&repair_status = repair.repair_status;
    GtkWidget *&repair_engine = repair.repair_engine;
    GtkWidget *&repair_sources = repair.repair_sources;
    GtkWidget *&repair_issues = repair.repair_issues;
    GtkWidget *&repair_health_banner = repair.repair_health_banner;
    GtkWidget *&repair_health_icon = repair.repair_health_icon;
    GtkWidget *&repair_health_title = repair.repair_health_title;
    GtkWidget *&repair_health_copy = repair.repair_health_copy;
    GtkWidget *&repair_recheck = repair.repair_recheck;
    GtkWidget *&repair_rebuild = repair.repair_rebuild;
    GtkWidget *&repair_configure = repair.repair_configure;
    unsigned int &repair_generation = repair.repair_generation;
    bool &repair_busy = repair.repair_busy;
    bool &repair_interrupted = repair.repair_interrupted;
    bool &repair_loaded = repair.repair_loaded;
};

} // namespace infiltrator::software::app

#endif
