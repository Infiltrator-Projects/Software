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

GtkWidget *make_repair_page(WindowState *state);
void refresh_repair(WindowState *state, bool refresh_metadata);

} // namespace infiltrator::software::app

#endif
