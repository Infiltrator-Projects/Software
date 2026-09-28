// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_HISTORY_VIEW_HPP
#define INFILTRATOR_SOFTWARE_HISTORY_VIEW_HPP

#include "core/transaction_history.hpp"

#include <gtk/gtk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace infiltrator::software {

std::string history_timestamp(std::int64_t unix_time);

void rebuild_history_view(
    GtkListBox *list,
    GtkWidget *count_label,
    const std::vector<TransactionHistoryItem> &records);

GtkWidget *create_history_page(
    GtkListBox **list_out,
    GtkWidget **status_out,
    GtkWidget **count_out,
    GtkWidget **refresh_out,
    GCallback refresh_callback,
    gpointer user_data);

} // namespace infiltrator::software

#endif
