// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DISCOVER_STATE_HPP
#define INFILTRATOR_SOFTWARE_DISCOVER_STATE_HPP

#include "core/model.hpp"

#include <gtk/gtk.h>

#include <string>
#include <vector>

namespace infiltrator::software::app {

/*
 * Mutable state owned by the Discover page. Keeping this outside WindowState
 * makes the page boundary explicit while the legacy shell is decomposed.
 */
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

} // namespace infiltrator::software::app

#endif
