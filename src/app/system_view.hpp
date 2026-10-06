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

#endif
