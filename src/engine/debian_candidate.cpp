// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_candidate.hpp"

#include "engine/debian_version.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

std::string package_base(const std::string_view id)
{
    const std::size_t colon = id.find(':');
    return std::string(
        colon == std::string_view::npos
            ? id
            : id.substr(0U, colon));
}

bool held(
    const PackageRecord &installed,
    const DebianCandidatePolicy &policy)
{
    if (policy.held_packages.find(installed.id) !=
        policy.held_packages.end()) {
        return true;
    }
    if (policy.held_packages.find(installed.package_name) !=
        policy.held_packages.end()) {
        return true;
    }
    return policy.held_packages.find(package_base(installed.package_name)) !=
           policy.held_packages.end();
}

int source_priority(
    const DebianPackageVersion &candidate,
    const DebianCandidatePolicy &policy)
{
    const auto configured =
        policy.source_priorities.find(candidate.source);
    if (configured != policy.source_priorities.end()) {
        return configured->second;
    }
    return policy.default_source_priority;
}

bool compatible_architecture(
    const PackageRecord &installed,
    const DebianPackageVersion &candidate)
{
    if (candidate.architecture == "all") {
        return true;
    }
    return !installed.architecture.empty() &&
           candidate.architecture == installed.architecture;
}

bool better_candidate(
    const DebianPackageVersion &left,
    const int left_priority,
    const DebianPackageVersion &right,
    const int right_priority,
    const PackageRecord &installed)
{
    if (left_priority != right_priority) {
        return left_priority > right_priority;
    }

    const int version =
        compare_debian_versions(left.version, right.version);
    if (version != 0) {
        return version > 0;
    }

    const bool left_exact =
        !installed.architecture.empty() &&
        left.architecture == installed.architecture;
    const bool right_exact =
        !installed.architecture.empty() &&
        right.architecture == installed.architecture;
    if (left_exact != right_exact) {
        return left_exact;
    }

    if (left.source != right.source) {
        return left.source < right.source;
    }
    return left.filename < right.filename;
}

} // namespace

std::vector<DebianCandidateSelection>
DebianCandidateSelector::select(
    const std::vector<PackageRecord> &installed,
    const std::vector<DebianPackageVersion> &available,
    const DebianCandidatePolicy &policy)
{
    std::vector<DebianCandidateSelection> selections;
    selections.reserve(installed.size());

    for (const PackageRecord &current : installed) {
        DebianCandidateSelection selection;
        selection.installed = current;
        selection.candidate_priority = policy.installed_priority;

        if (held(current, policy)) {
            selection.held = true;
            selection.reason = "Package is held.";
            selections.emplace_back(std::move(selection));
            continue;
        }

        const std::string base = package_base(current.package_name);
        const DebianPackageVersion *best = nullptr;
        int best_priority = 0;

        for (const DebianPackageVersion &candidate : available) {
            if (candidate.package != base ||
                !compatible_architecture(current, candidate)) {
                continue;
            }

            const int priority = source_priority(candidate, policy);
            if (priority <= 0) {
                continue;
            }

            if (best == nullptr ||
                better_candidate(
                    candidate, priority,
                    *best, best_priority,
                    current)) {
                best = &candidate;
                best_priority = priority;
            }
        }

        if (best == nullptr) {
            selection.reason = "No compatible repository candidate.";
            selections.emplace_back(std::move(selection));
            continue;
        }

        const int version_order =
            compare_debian_versions(
                best->version,
                current.installed_version);

        if (best_priority < policy.installed_priority) {
            selection.reason =
                "Installed version has higher package priority.";
            selections.emplace_back(std::move(selection));
            continue;
        }

        if (version_order < 0 && best_priority <= 1000) {
            selection.reason =
                "Repository candidate is older; downgrade is not permitted.";
            selections.emplace_back(std::move(selection));
            continue;
        }

        selection.candidate = *best;
        selection.candidate_priority = best_priority;

        if (version_order > 0) {
            selection.upgrade_available = true;
            selection.reason = "Newer repository candidate selected.";
        } else if (version_order < 0) {
            selection.downgrade_selected = true;
            selection.reason =
                "Older repository candidate selected by priority above 1000.";
        } else {
            selection.reason = "Installed version is current.";
        }

        selections.emplace_back(std::move(selection));
    }

    std::sort(
        selections.begin(), selections.end(),
        [](const DebianCandidateSelection &left,
           const DebianCandidateSelection &right) {
            return left.installed.id < right.installed.id;
        });

    return selections;
}

} // namespace infiltrator::software
