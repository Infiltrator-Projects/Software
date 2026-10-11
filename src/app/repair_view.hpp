// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_REPAIR_VIEW_HPP
#define INFILTRATOR_SOFTWARE_REPAIR_VIEW_HPP

#include <gtk/gtk.h>

namespace infiltrator::software::app {

struct WindowState;

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

inline bool repair_page_loaded(
    const RepairPageState &state) noexcept
{
    return state.repair_loaded;
}

GtkWidget *make_repair_page(WindowState *state);
void refresh_repair(WindowState *state, bool refresh_metadata);

} // namespace infiltrator::software::app

/*
 * Temporary source-compatibility spellings exist only for the unmigrated
 * Repair/legacy implementation units.  The coordinator is deliberately
 * excluded so cross-page code can only use the public RepairPageState boundary.
 */
#if !defined(INFILTRATOR_SOFTWARE_WINDOW_STATE_HPP) && \
    !defined(INFILTRATOR_SOFTWARE_KERNEL_MANAGER_HPP) && \
    !defined(INFILTRATOR_SOFTWARE_APP_COORDINATOR_HPP)
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
#define discover_health_banner_state discover.discover_health_banner_state
#define discover_health_summary discover.discover_health_summary
#endif

#endif
