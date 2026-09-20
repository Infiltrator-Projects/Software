// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_transaction.hpp"

#include "engine/debian_dependency.hpp"
#include "engine/debian_version.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

std::string package_base(const std::string_view identity)
{
    const std::size_t colon = identity.find(':');
    return std::string(
        colon == std::string_view::npos
            ? identity
            : identity.substr(0U, colon));
}

std::string package_architecture(const std::string_view identity)
{
    const std::size_t colon = identity.find(':');
    return colon == std::string_view::npos
        ? std::string{}
        : std::string(identity.substr(colon + 1U));
}

bool held(
    const std::string_view identity,
    const DebianCandidatePolicy &policy)
{
    const std::string base = package_base(identity);
    return
        policy.held_packages.find(std::string(identity)) !=
            policy.held_packages.end() ||
        policy.held_packages.find(base) !=
            policy.held_packages.end();
}

int source_priority(
    const DebianPackageVersion &candidate,
    const DebianCandidatePolicy &policy)
{
    const auto configured =
        policy.source_priorities.find(candidate.source);
    return configured == policy.source_priorities.end()
        ? policy.default_source_priority
        : configured->second;
}

bool architecture_matches(
    const DebianPackageVersion &candidate,
    const std::string_view requested_architecture,
    const std::string_view target_architecture)
{
    if (!requested_architecture.empty()) {
        return candidate.architecture == requested_architecture ||
               candidate.architecture == "all";
    }

    if (target_architecture.empty()) {
        return true;
    }

    return candidate.architecture == target_architecture ||
           candidate.architecture == "all";
}

bool better_root_candidate(
    const DebianPackageVersion &left,
    const DebianPackageVersion &right,
    const std::string_view requested_architecture,
    const std::string_view target_architecture,
    const DebianCandidatePolicy &policy)
{
    const int left_priority = source_priority(left, policy);
    const int right_priority = source_priority(right, policy);
    if (left_priority != right_priority) {
        return left_priority > right_priority;
    }

    const int version =
        compare_debian_versions(left.version, right.version);
    if (version != 0) {
        return version > 0;
    }

    const std::string_view preferred_architecture =
        requested_architecture.empty()
            ? target_architecture
            : requested_architecture;
    const bool left_exact =
        !preferred_architecture.empty() &&
        left.architecture == preferred_architecture;
    const bool right_exact =
        !preferred_architecture.empty() &&
        right.architecture == preferred_architecture;
    if (left_exact != right_exact) {
        return left_exact;
    }

    if (left.source != right.source) {
        return left.source < right.source;
    }
    return left.filename < right.filename;
}

const DebianPackageVersion *select_install_root(
    const std::string_view identity,
    const std::vector<DebianPackageVersion> &available,
    const std::string_view target_architecture,
    const DebianCandidatePolicy &policy)
{
    const std::string base = package_base(identity);
    const std::string requested_architecture =
        package_architecture(identity);

    const DebianPackageVersion *best = nullptr;
    for (const DebianPackageVersion &candidate : available) {
        if (candidate.package != base ||
            !architecture_matches(
                candidate,
                requested_architecture,
                target_architecture) ||
            source_priority(candidate, policy) <= 0) {
            continue;
        }

        if (best == nullptr ||
            better_root_candidate(
                candidate,
                *best,
                requested_architecture,
                target_architecture,
                policy)) {
            best = &candidate;
        }
    }
    return best;
}

const PackageRecord *find_installed_request(
    const std::string_view identity,
    const std::vector<PackageRecord> &installed,
    const std::string_view target_architecture)
{
    const std::string base = package_base(identity);
    const std::string requested_architecture =
        package_architecture(identity);

    const PackageRecord *fallback = nullptr;
    for (const PackageRecord &package : installed) {
        if (package.id == identity ||
            package.package_name == identity) {
            return &package;
        }

        if (package_base(package.package_name) != base) {
            continue;
        }

        if (!requested_architecture.empty()) {
            if (package.architecture == requested_architecture) {
                return &package;
            }
            continue;
        }

        if (!target_architecture.empty() &&
            package.architecture == target_architecture) {
            return &package;
        }

        if (fallback == nullptr) {
            fallback = &package;
        } else if (
            fallback->architecture != package.architecture) {
            return nullptr;
        }
    }

    return fallback;
}

const PackageRecord *find_installed_candidate(
    const DebianPackageVersion &candidate,
    const std::vector<PackageRecord> &installed)
{
    const PackageRecord *fallback = nullptr;
    for (const PackageRecord &package : installed) {
        if (package_base(package.package_name) != candidate.package) {
            continue;
        }

        if (package.architecture == candidate.architecture) {
            return &package;
        }

        if (candidate.architecture == "all" && fallback == nullptr) {
            fallback = &package;
        }
    }
    return fallback;
}

std::optional<DebianPackageVersion> upgrade_root(
    const PackageRecord &current,
    const std::vector<DebianPackageVersion> &available,
    const DebianCandidatePolicy &policy,
    std::string &error)
{
    const auto selections =
        DebianCandidateSelector::select(
            {current}, available, policy);
    if (selections.empty()) {
        error =
            "Unable to evaluate an upgrade candidate for " +
            current.id + ".";
        return std::nullopt;
    }

    const DebianCandidateSelection &selection = selections.front();
    if (selection.held) {
        error = "Package is held: " + current.id + ".";
        return std::nullopt;
    }

    if (!selection.candidate.has_value() ||
        (!selection.upgrade_available &&
         !selection.downgrade_selected)) {
        error =
            "No package change is available for " +
            current.id + ".";
        return std::nullopt;
    }

    return selection.candidate;
}

std::int64_t signed_size_delta(
    const std::uint64_t after,
    const std::uint64_t before) noexcept
{
    constexpr std::uint64_t maximum =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max());

    if (after >= before) {
        const std::uint64_t difference = after - before;
        return difference > maximum
            ? std::numeric_limits<std::int64_t>::max()
            : static_cast<std::int64_t>(difference);
    }

    const std::uint64_t difference = before - after;
    if (difference > maximum) {
        return std::numeric_limits<std::int64_t>::min() + 1;
    }
    return -static_cast<std::int64_t>(difference);
}

std::int64_t add_delta(
    const std::int64_t left,
    const std::int64_t right) noexcept
{
    if (right > 0 &&
        left > std::numeric_limits<std::int64_t>::max() - right) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (right < 0 &&
        left < std::numeric_limits<std::int64_t>::min() - right) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return left + right;
}

std::uint64_t add_download(
    const std::uint64_t left,
    const std::uint64_t right) noexcept
{
    if (left >
        std::numeric_limits<std::uint64_t>::max() - right) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

bool system_critical(
    const DebianPackageVersion &candidate)
{
    if (candidate.essential ||
        candidate.priority == "required") {
        return true;
    }

    const std::string &name = candidate.package;
    return
        name == "dpkg" ||
        name == "systemd" ||
        name == "libc6" ||
        name == "linux-base" ||
        name == "infiltrator-software" ||
        name.rfind("linux-image", 0U) == 0U ||
        name.rfind("linux-modules", 0U) == 0U;
}

bool explicitly_requested(
    const DebianPackageVersion &candidate,
    const std::unordered_set<std::string> &requested)
{
    if (requested.find(candidate.package) != requested.end()) {
        return true;
    }

    return requested.find(
               candidate.package + ":" +
               candidate.architecture) != requested.end();
}

std::string resolution_error(
    const DebianResolution &resolution)
{
    std::string result =
        "Native dependency resolution could not produce a safe transaction.";
    for (const DebianResolutionProblem &problem :
         resolution.problems) {
        result += "\n" + problem.package + ": ";
        if (!problem.expression.empty()) {
            result += problem.expression + " — ";
        }
        result += problem.detail;
    }
    return result;
}

} // namespace

std::optional<TransactionPlan> DebianTransactionPlanner::plan(
    const TransactionRequest &request,
    const std::vector<PackageRecord> &installed,
    const std::vector<DebianPackageVersion> &available,
    const std::string_view target_architecture,
    const std::uint64_t state_generation,
    const std::string_view source_fingerprint,
    const DebianCandidatePolicy &policy,
    std::string &error)
{
    error.clear();

    if (request.package_ids.empty()) {
        error = "No packages were selected for the transaction.";
        return std::nullopt;
    }

    if (request.action == TransactionAction::remove) {
        error =
            "Native removal planning is not enabled until installed "
            "reverse-dependency state is represented in the shared package "
            "database.";
        return std::nullopt;
    }

    std::vector<DebianPackageVersion> roots;
    std::unordered_set<std::string> root_keys;
    std::unordered_set<std::string> requested;

    for (const std::string &identity : request.package_ids) {
        if (identity.empty()) {
            error = "Transaction package identity is empty.";
            return std::nullopt;
        }

        requested.insert(identity);
        requested.insert(package_base(identity));

        std::optional<DebianPackageVersion> root;

        if (request.action == TransactionAction::upgrade) {
            const PackageRecord *current =
                find_installed_request(
                    identity, installed, target_architecture);
            if (current == nullptr) {
                error =
                    "Selected upgrade package is not installed or is "
                    "architecture-ambiguous: " + identity + ".";
                return std::nullopt;
            }

            root =
                upgrade_root(
                    *current, available, policy, error);
            if (!root.has_value()) {
                return std::nullopt;
            }
        } else {
            if (held(identity, policy)) {
                error = "Package is held: " + identity + ".";
                return std::nullopt;
            }

            const DebianPackageVersion *candidate =
                select_install_root(
                    identity,
                    available,
                    target_architecture,
                    policy);
            if (candidate == nullptr) {
                error =
                    "No repository candidate is available for " +
                    identity + ".";
                return std::nullopt;
            }
            root = *candidate;

            const PackageRecord *current =
                find_installed_candidate(*candidate, installed);
            if (current != nullptr &&
                current->architecture != candidate->architecture &&
                candidate->architecture != "all" &&
                candidate->multi_arch != "same") {
                error =
                    "Installing " + candidate->package + ":" +
                    candidate->architecture +
                    " beside the installed architecture is not permitted "
                    "by its Multi-Arch metadata.";
                return std::nullopt;
            }
        }

        const std::string key =
            root->package + ":" + root->architecture + "=" +
            root->version;
        if (root_keys.insert(key).second) {
            roots.emplace_back(std::move(*root));
        }
    }

    const DebianResolution resolution =
        DebianDependencyResolver::resolve(
            roots,
            installed,
            available,
            target_architecture,
            policy);
    if (!resolution.complete()) {
        error = resolution_error(resolution);
        return std::nullopt;
    }

    TransactionPlan plan;
    plan.state_generation = state_generation;
    plan.source_fingerprint = std::string(source_fingerprint);

    for (const DebianPackageVersion &candidate :
         resolution.selected) {
        const PackageRecord *current =
            find_installed_candidate(candidate, installed);

        if (current != nullptr &&
            current->installed_version == candidate.version) {
            continue;
        }

        if (current != nullptr &&
            held(current->id, policy)) {
            error =
                "Resolved transaction would modify held package " +
                current->id + ".";
            return std::nullopt;
        }

        if (current != nullptr &&
            compare_debian_versions(
                candidate.version,
                current->installed_version) < 0 &&
            source_priority(candidate, policy) <= 1000) {
            error =
                "Resolved transaction would downgrade " +
                current->id +
                " without repository priority above 1000.";
            return std::nullopt;
        }

        TransactionItem item;
        item.package_id =
            candidate.architecture.empty() ||
            candidate.architecture == "all"
                ? candidate.package
                : candidate.package + ":" +
                    candidate.architecture;
        item.architecture = candidate.architecture;
        item.source = candidate.source;
        item.filename = candidate.filename;
        item.sha256 = candidate.sha256;
        item.to_version = candidate.version;
        item.download_bytes = candidate.size_bytes;
        item.requested =
            explicitly_requested(candidate, requested);
        item.system_critical =
            system_critical(candidate);

        if (current == nullptr) {
            item.action = TransactionAction::install;
            item.disk_delta_bytes =
                signed_size_delta(
                    candidate.installed_size_bytes, 0U);
        } else {
            item.action = TransactionAction::upgrade;
            item.from_version = current->installed_version;
            item.disk_delta_bytes =
                signed_size_delta(
                    candidate.installed_size_bytes,
                    current->installed_size_bytes);
        }

        plan.download_bytes =
            add_download(
                plan.download_bytes,
                item.download_bytes);
        plan.disk_delta_bytes =
            add_delta(
                plan.disk_delta_bytes,
                item.disk_delta_bytes);
        plan.touches_system =
            plan.touches_system ||
            item.system_critical;

        plan.items.emplace_back(std::move(item));
    }

    if (plan.items.empty()) {
        error =
            "The selected packages already match the resolved repository "
            "state; there is no transaction to perform.";
        return std::nullopt;
    }

    std::sort(
        plan.items.begin(),
        plan.items.end(),
        [](const TransactionItem &left,
           const TransactionItem &right) {
            if (left.requested != right.requested) {
                return left.requested > right.requested;
            }
            if (left.system_critical != right.system_critical) {
                return left.system_critical > right.system_critical;
            }
            return left.package_id < right.package_id;
        });

    return plan;
}

} // namespace infiltrator::software
