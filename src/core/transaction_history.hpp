// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_TRANSACTION_HISTORY_HPP
#define INFILTRATOR_SOFTWARE_TRANSACTION_HISTORY_HPP

#include "core/model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct TransactionHistoryItem {
    std::int64_t transaction_id{0};
    std::int64_t completed_at_unix{0};
    TransactionAction action{TransactionAction::install};
    bool success{false};
    std::string message;
    std::string package_id;
    std::string from_version;
    std::string to_version;
    std::string source;
    bool requested{false};
    bool system_critical{false};
};

class TransactionHistoryStore final {
public:
    explicit TransactionHistoryStore(std::string path);

    [[nodiscard]] const std::string &path() const noexcept;

    bool append(
        const TransactionPlan &plan,
        bool success,
        std::string_view message,
        std::string &error) const;

    std::vector<TransactionHistoryItem> load_recent(
        std::size_t limit,
        std::string &error) const;

private:
    std::string path_;
};

} // namespace infiltrator::software

#endif
