// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_SYSTEM_HISTORY_HPP
#define INFILTRATOR_SOFTWARE_SYSTEM_HISTORY_HPP

#include "core/transaction_history.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace infiltrator::software {

std::vector<TransactionHistoryItem> load_system_update_history(
    std::size_t limit,
    std::string &error);

} // namespace infiltrator::software

#endif
