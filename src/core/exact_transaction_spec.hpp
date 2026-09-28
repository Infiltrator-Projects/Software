// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_EXACT_TRANSACTION_SPEC_HPP
#define INFILTRATOR_SOFTWARE_EXACT_TRANSACTION_SPEC_HPP

#include "core/model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct ExactTransactionSpec {
    TransactionAction action{TransactionAction::install};
    std::string package_id;
    std::string version;
    std::string architecture;
    std::string source;
    std::string filename;
    std::string sha256;
};

bool encode_exact_transaction_spec(
    const TransactionItem &item,
    std::string &spec,
    std::string &error);

bool exact_transaction_specs(
    const TransactionPlan &plan,
    std::vector<std::string> &specs,
    std::string &error);

bool decode_exact_transaction_spec(
    std::string_view spec,
    ExactTransactionSpec &decoded,
    std::string &error);

} // namespace infiltrator::software

#endif
