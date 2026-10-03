// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_WINDOW_STATE_HPP
#define INFILTRATOR_SOFTWARE_WINDOW_STATE_HPP

#include "app/theme.hpp"
#include "app/history_controller.hpp"
#include "app/installed_controller.hpp"
#include "app/repository_controller.hpp"
#include "core/model.hpp"
#include "core/update_policy.hpp"
#include "external/external_updates.hpp"

#include <gtk/gtk.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace infiltrator::software::app {

struct WindowState {
    GtkWindow *window{};
    GtkStack *stack{};
    ThemeController theme;
    GtkWidget *maximize_button{};
    GtkWidget *global_search{};
    bool search_syncing{false};
    InstalledController installed;

    GtkStringList *discover_visible{};
    GtkWidget *discover_search{};
    GtkWidget *discover_category{};
    GtkStringList *discover_categories{};
    GtkListBox *navigation_list{};
    GtkWidget *nav_updates_badge{};
    GtkWidget *discover_status{};
    GtkWidget *discover_count{};
    GtkWidget *discover_source{};
    GtkWidget *discover_state{};
    GtkWidget *discover_featured_flow{};
    GtkWidget *discover_update_preview{};
    GtkWidget *discover_repository_preview{};
    GtkWidget *discover_activity_preview{};
    GtkWidget *discover_health_banner_state{};
    GtkWidget *discover_updates_summary{};
    GtkWidget *discover_health_summary{};
    GtkWidget *discover_repositories_summary{};
    std::vector<PackageRecord> discover_records;
    std::vector<std::string> discover_search_texts;
    unsigned int discover_generation{0U};

    RepositoryController repositories;

    GtkListBox *updates_list{};
    GtkWidget *updates_status{};
    GtkWidget *updates_count{};
    GtkWidget *updates_critical{};
    GtkWidget *updates_install{};
    GtkWidget *updates_security{};
    GtkWidget *updates_refresh{};
    GtkWidget *updates_reboot_banner{};
    GtkWidget *updates_reboot_detail{};
    GtkWidget *updates_backend{};
    GtkListBox *external_updates_list{};
    GtkWidget *external_updates_status{};
    GtkWidget *external_updates_spinner{};
    GtkWidget *external_flatpak_apply{};
    GtkWidget *external_cinnamon_apply{};
    std::vector<ExternalUpdate> external_update_records;
    std::unordered_set<std::string> selected_flatpak_refs;
    std::unordered_set<std::string> selected_cinnamon_refs;
    GtkWidget *updates_transaction_panel{};
    GtkWidget *updates_transaction_phase{};
    GtkWidget *updates_transaction_detail{};
    GtkWidget *updates_transaction_meta{};
    GtkWidget *updates_stage_labels[6]{};
    GtkWidget *updates_progress{};
    guint updates_progress_timer_id{0U};
    gint64 updates_progress_started_us{0};
    std::size_t updates_progress_items{0U};
    std::uint64_t updates_progress_download_bytes{0U};
    std::string updates_progress_token;
    std::string updates_progress_phase;
    bool updates_post_install_refresh{false};
    bool updates_restart_after_verify{false};
    std::vector<PackageRecord> update_records;
    std::unordered_set<std::string> selected_update_ids;
    std::string pending_update_selection;
    std::optional<TransactionPlan> pending_update_plan;
    unsigned int updates_generation{0U};
    bool updates_busy{false};
    bool external_updates_active{false};
    bool updates_auto_refresh_pending{true};
    gint64 updates_last_metadata_refresh_us{0};
    SoftwarePreferences preferences{};
    guint updates_refresh_timer_id{0U};

    GtkListBox *system_list{};
    GtkWidget *system_status{};
    GtkWidget *system_count{};
    GtkWidget *system_updates{};
    GtkWidget *system_critical{};
    GtkWidget *system_refresh{};
    GtkWidget *system_review_updates{};
    std::vector<PackageRecord> system_records;
    std::vector<PackageRecord> system_update_records;
    unsigned int system_generation{0U};
    bool system_busy{false};

    HistoryController history;

    GtkListBox *repair_list{};
    GtkWidget *repair_status{};
    GtkWidget *repair_engine{};
    GtkWidget *repair_sources{};
    GtkWidget *repair_issues{};
    GtkWidget *repair_health_banner{};
    GtkWidget *repair_health_icon{};
    GtkWidget *repair_health_title{};
    GtkWidget *repair_health_copy{};
    GtkWidget *repair_recheck{};
    GtkWidget *repair_rebuild{};
    GtkWidget *repair_configure{};
    unsigned int repair_generation{0U};
    bool repair_busy{false};
    bool repair_interrupted{false};

    bool window_presented{false};
    bool discover_loaded{false};
    bool updates_loaded{false};
    bool system_loaded{false};
    bool repair_loaded{false};
};

} // namespace infiltrator::software::app

#endif
