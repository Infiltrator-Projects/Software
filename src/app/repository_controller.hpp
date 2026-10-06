// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_REPOSITORY_CONTROLLER_HPP
#define INFILTRATOR_SOFTWARE_REPOSITORY_CONTROLLER_HPP

#include "core/model.hpp"

#include <gtk/gtk.h>

#include <vector>

namespace infiltrator::software {

struct RepositoryController {
    GtkWindow *window{};
    GtkWidget *flow{};
    GtkWidget *count{};
    GtkWidget *status{};
    std::vector<SourceRecord> records;
    unsigned int generation{0U};
    bool busy{false};
    bool loaded{false};

    GtkWidget *(*make_card)(gpointer, const SourceRecord &){};
    void (*changed)(gpointer){};
    gpointer callback_data{};
};

void configure_repository_controller(
    RepositoryController *controller,
    GtkWindow *window,
    GtkWidget *flow,
    GtkWidget *count,
    GtkWidget *status,
    GtkWidget *(*make_card)(gpointer, const SourceRecord &),
    void (*changed)(gpointer),
    gpointer callback_data);

void refresh_repository_controller(
    RepositoryController *controller);

} // namespace infiltrator::software

#endif
