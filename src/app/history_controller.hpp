// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_HISTORY_CONTROLLER_HPP
#define INFILTRATOR_SOFTWARE_HISTORY_CONTROLLER_HPP

#include "core/transaction_history.hpp"

#include <gtk/gtk.h>

#include <vector>

namespace infiltrator::software {

struct HistoryController {
    GtkWindow *window{};
    GtkListBox *list{};
    GtkWidget *status{};
    GtkWidget *count{};
    GtkWidget *refresh{};
    std::vector<TransactionHistoryItem> records;
    unsigned int generation{0U};
    bool busy{false};
    bool loaded{false};
    void (*changed)(gpointer){};
    gpointer changed_data{};
};

inline bool history_controller_loaded(
    const HistoryController &controller) noexcept
{
    return controller.loaded;
}

GtkWidget *create_history_controller_page(
    HistoryController *controller,
    GtkWindow *window,
    void (*changed)(gpointer),
    gpointer changed_data);

void refresh_history_controller(
    HistoryController *controller);

} // namespace infiltrator::software

#endif
