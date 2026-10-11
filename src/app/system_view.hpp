// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_SYSTEM_VIEW_HPP
#define INFILTRATOR_SOFTWARE_SYSTEM_VIEW_HPP

#include "core/model.hpp"

#include <gtk/gtk.h>

#include <vector>

namespace infiltrator::software::app {

struct WindowState;

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

inline bool system_page_loaded(
    const SystemPageState &state) noexcept
{
    return state.system_loaded;
}

GtkWidget *make_system_page(WindowState *state);
void refresh_system(WindowState *state, bool refresh_metadata);
void system_manage_kernels_clicked(GtkButton *button, gpointer user_data);

} // namespace infiltrator::software::app

/*
 * system_view.cpp is being converted to explicit owner access.  Keep the
 * historical spellings local to that translation unit while the conversion
 * finishes.  system_critical intentionally is not a macro because PackageRecord
 * has a field with the same name.
 */
#if !defined(INFILTRATOR_SOFTWARE_WINDOW_STATE_HPP) && \
    !defined(INFILTRATOR_SOFTWARE_KERNEL_MANAGER_HPP)
#define system_list system.system_list
#define system_status system.system_status
#define system_count system.system_count
#define system_updates system.system_updates
#define system_refresh system.system_refresh
#define system_review_updates system.system_review_updates
#define system_records system.system_records
#define system_update_records system.system_update_records
#define system_generation system.system_generation
#define system_busy system.system_busy
#define system_loaded system.system_loaded
#endif

#endif
