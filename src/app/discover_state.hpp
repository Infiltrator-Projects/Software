// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DISCOVER_STATE_HPP
#define INFILTRATOR_SOFTWARE_DISCOVER_STATE_HPP

#include "core/model.hpp"

#include <gtk/gtk.h>

#include <cstddef>
#include <string>
#include <vector>

/*
 * A pair of temporary Repair compatibility spellings may be active in the
 * legacy translation unit before this owner header is reached.  Never allow
 * those macros to rewrite the actual DiscoverPageState declaration or its
 * owner-local helpers; restore them only after this header has been parsed.
 */
#ifdef discover_health_banner_state
#pragma push_macro("discover_health_banner_state")
#undef discover_health_banner_state
#define INFILTRATOR_RESTORE_DISCOVER_HEALTH_BANNER_STATE 1
#endif
#ifdef discover_health_summary
#pragma push_macro("discover_health_summary")
#undef discover_health_summary
#define INFILTRATOR_RESTORE_DISCOVER_HEALTH_SUMMARY 1
#endif

namespace infiltrator::software::app {

/* Mutable state owned exclusively by the Discover page. */
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

inline bool discover_page_loaded(
    const DiscoverPageState &state) noexcept
{
    return state.discover_loaded;
}

inline void set_discover_repository_summary(
    DiscoverPageState &state,
    const std::size_t repository_count)
{
    if (state.discover_repositories_summary == nullptr) {
        return;
    }
    const std::string count = std::to_string(repository_count);
    gtk_label_set_text(
        GTK_LABEL(state.discover_repositories_summary),
        count.c_str());
}

} // namespace infiltrator::software::app

#ifdef INFILTRATOR_RESTORE_DISCOVER_HEALTH_SUMMARY
#pragma pop_macro("discover_health_summary")
#undef INFILTRATOR_RESTORE_DISCOVER_HEALTH_SUMMARY
#endif
#ifdef INFILTRATOR_RESTORE_DISCOVER_HEALTH_BANNER_STATE
#pragma pop_macro("discover_health_banner_state")
#undef INFILTRATOR_RESTORE_DISCOVER_HEALTH_BANNER_STATE
#endif

#endif
