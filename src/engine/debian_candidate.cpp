// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_candidate.hpp"

#include "engine/debian_version.hpp"

#include <algorithm>
#include <cctype>
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
    if (installed.held) {
        return true;
    }
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
    if (candidate.pin_priority != 0) {
        return candidate.pin_priority;
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

/*
 * Repository source identifiers deliberately retain their configured file,
 * line/stanza and expansion ordinal.  For equal version/priority candidates,
 * Debian APT selects the instance from the source configured earliest.  A raw
 * lexical comparison is subtly wrong once a numeric component reaches two
 * digits (":10" sorts before ":2").  Compare digit runs by numeric magnitude
 * without converting them to an integer, so arbitrarily long identifiers stay
 * deterministic and cannot overflow.
 */
bool source_precedes(
    const std::string_view left,
    const std::string_view right)
{
    std::size_t left_index = 0U;
    std::size_t right_index = 0U;

    while (left_index < left.size() &&
           right_index < right.size()) {
        const unsigned char left_character =
            static_cast<unsigned char>(left[left_index]);
        const unsigned char right_character =
            static_cast<unsigned char>(right[right_index]);

        if (std::isdigit(left_character) != 0 &&
            std::isdigit(right_character) != 0) {
            const std::size_t left_begin = left_index;
            const std::size_t right_begin = right_index;

            while (left_index < left.size() &&
                   std::isdigit(static_cast<unsigned char>(
                       left[left_index])) != 0) {
                ++left_index;
            }
            while (right_index < right.size() &&
                   std::isdigit(static_cast<unsigned char>(
                       right[right_index])) != 0) {
                ++right_index;
            }

            std::size_t left_significant = left_begin;
            while (left_significant < left_index &&
                   left[left_significant] == '0') {
                ++left_significant;
            }
            std::size_t right_significant = right_begin;
            while (right_significant < right_index &&
                   right[right_significant] == '0') {
                ++right_significant;
            }

            const std::size_t left_digits =
                left_index - left_significant;
            const std::size_t right_digits =
                right_index - right_significant;
            if (left_digits != right_digits) {
                return left_digits < right_digits;
            }

            if (left_digits != 0U) {
                const int numeric_order =
                    left.substr(left_significant, left_digits).compare(
                        right.substr(
                            right_significant,
                            right_digits));
                if (numeric_order != 0) {
                    return numeric_order < 0;
                }
            }

            const std::size_t left_run = left_index - left_begin;
            const std::size_t right_run = right_index - right_begin;
            if (left_run != right_run) {
                return left_run < right_run;
            }
            continue;
        }

        if (left_character != right_character) {
            return left_character < right_character;
        }
        ++left_index;
        ++right_index;
    }

    return left.size() < right.size();
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
        return source_precedes(left.source, right.source);
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
