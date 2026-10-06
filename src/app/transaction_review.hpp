// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_TRANSACTION_REVIEW_HPP
#define INFILTRATOR_SOFTWARE_TRANSACTION_REVIEW_HPP

#include "core/model.hpp"

#include <gtk/gtk.h>

#include <string>

namespace infiltrator::software::app {

GtkWidget *make_transaction_review_dialog(
    GtkWindow *parent,
    const char *title,
    const std::string &heading,
    const char *accept_label,
    const TransactionPlan &plan);

} // namespace infiltrator::software::app

#endif
