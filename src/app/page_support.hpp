// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTWARE_PAGE_SUPPORT_HPP
#define SOFTWARE_PAGE_SUPPORT_HPP

#include "core/model.hpp"
#include <gtk/gtk.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace infiltrator::software::app {

std::string display_size(std::uint64_t bytes);
std::string one_line(std::string value);
const char *update_icon_name(const PackageRecord &package);
void package_record_destroy(gpointer data);
GtkWidget *detail_row(const char *caption, const std::string &value);
void record_transaction_history(
    const TransactionPlan &plan,
    bool success,
    std::string_view message);

} // namespace infiltrator::software::app
#endif
