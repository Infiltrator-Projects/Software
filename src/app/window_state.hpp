// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_WINDOW_STATE_HPP
#define INFILTRATOR_SOFTWARE_WINDOW_STATE_HPP

#include "app/theme.hpp"
#include "app/history_controller.hpp"
#include "app/installed_controller.hpp"
#include "app/repository_controller.hpp"
#include "app/updates_controller.hpp"
#include "core/model.hpp"
#include "core/update_policy.hpp"

#include <gtk/gtk.h>

#include <string>
#include <vector>

namespace infiltrator::software::app {

/*
 * WindowState used to be one flat mutable bag shared by every page. Keep the
 * source-compatible member names during the 0.3 -> 0.4 migration, but split
 * ownership by responsibility so page code can progressively accept only the
 * state it actually needs instead of the whole application.
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

struct DiscoverPageState {
    GtkStringList *discover_visible{};
    GtkWidget *discover_search{};
    GtkWidget *discover_category{};
    GtkStringList *discover_categories{};
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
    bool discover_loaded{false};
};

struct SystemPageState {
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
    bool system_loaded{false};
};

struct RepairPageState {
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
    bool repair_loaded{false};
};

/*
 * UpdatesController owns the Updates page state. Public inheritance is a
 * temporary source-compatibility bridge only; new Updates code should accept
 * UpdatesController directly rather than WindowState.
 */
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
