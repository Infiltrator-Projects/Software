// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_CANDIDATE_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_CANDIDATE_HPP

#include "core/model.hpp"
#include "engine/debian_package_index.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace infiltrator::software {

struct DebianCandidatePolicy {
    int default_source_priority{500};
    int installed_priority{100};
    std::unordered_map<std::string, int> source_priorities;
    std::unordered_set<std::string> held_packages;
};

struct DebianCandidateSelection {
    PackageRecord installed;
    std::optional<DebianPackageVersion> candidate;
    int candidate_priority{0};
    bool upgrade_available{false};
    bool downgrade_selected{false};
    bool held{false};
    std::string reason;
};

class DebianCandidateSelector final {
public:
    static std::vector<DebianCandidateSelection> select(
        const std::vector<PackageRecord> &installed,
        const std::vector<DebianPackageVersion> &available,
        const DebianCandidatePolicy &policy = {});
};

} // namespace infiltrator::software

#endif
