// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_TRANSACTION_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_TRANSACTION_HPP

#include "core/model.hpp"
#include "engine/debian_candidate.hpp"
#include "engine/debian_package_index.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

class DebianTransactionPlanner final {
public:
    static std::optional<TransactionPlan> plan(
        const TransactionRequest &request,
        const std::vector<PackageRecord> &installed,
        const std::vector<DebianPackageVersion> &available,
        std::string_view target_architecture,
        std::uint64_t state_generation,
        std::string_view source_fingerprint,
        const DebianCandidatePolicy &policy,
        std::string &error);
};

} // namespace infiltrator::software

#endif
