// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/engine_service_core.hpp"

#include "engine/debian_reconcile.hpp"
#include "engine/debian_transaction.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <string_view>
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

std::vector<PackageRecord> build_updates(
    const PackageStateSnapshot &snapshot,
    const DebianCandidatePolicy &policy)
{
    std::vector<PackageRecord> result;
    const auto selections =
        DebianCandidateSelector::select(
            snapshot.installed,
            snapshot.available,
            policy);

    for (const DebianCandidateSelection &selection : selections) {
        if (!selection.candidate.has_value() ||
            (!selection.upgrade_available &&
             !selection.downgrade_selected)) {
            continue;
        }

        const DebianPackageVersion &candidate =
            *selection.candidate;
        PackageRecord record = selection.installed;
        record.available_version = candidate.version;
        record.source = candidate.source;
        record.repository_origin = candidate.release_origin;
        record.repository_site = candidate.site;
        record.policy_provider = candidate.policy_provider;
        record.policy_reason = candidate.policy_reason;
        record.selection_reason = selection.reason;
        record.candidate_priority = selection.candidate_priority;
        record.asset = candidate.filename;
        record.package_sha256 = candidate.sha256;
        record.download_size_bytes = candidate.size_bytes;
        record.priority = candidate.priority;
        record.essential = candidate.essential;
        record.state = InstallState::upgradable;
        classify_package_role(record);
        result.emplace_back(std::move(record));
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const PackageRecord &left,
           const PackageRecord &right) {
            if (left.system_critical != right.system_critical) {
                return left.system_critical >
                       right.system_critical;
            }
            if (left.kind != right.kind) {
                return static_cast<int>(left.kind) <
                       static_cast<int>(right.kind);
            }
            return left.name < right.name;
        });

    return result;
}

} // namespace

EngineServiceCore::EngineServiceCore(
    std::string state_database_path,
    DebianCandidatePolicy policy)
    : store_(std::move(state_database_path)),
      policy_(std::move(policy))
{
}

bool EngineServiceCore::reload(std::string &error)
{
    std::string load_error;
    auto loaded = store_.load_current(load_error);
    if (!loaded.has_value()) {
        healthy_ = false;
        detail_ = load_error.empty()
            ? "No published package-state generation is available."
            : load_error;
        error = detail_;
        return false;
    }

    if (snapshot_.has_value() &&
        snapshot_->generation == loaded->generation &&
        snapshot_->source_fingerprint ==
            loaded->source_fingerprint) {
        healthy_ = true;
        detail_ = "Ready";
        error.clear();
        return true;
    }

    std::vector<PackageRecord> new_updates =
        build_updates(*loaded, policy_);

    snapshot_ = std::move(*loaded);
    updates_ = std::move(new_updates);
    healthy_ = true;
    detail_ = "Ready";
    error.clear();
    return true;
}

bool EngineServiceCore::refresh(std::string &error)
{
    std::uint64_t published_generation = 0U;
    std::string refresh_error;
    if (!DebianReconciler::reconcile(
            store_,
            native_debian_architecture(),
            default_repository_cache_path(),
            published_generation,
            refresh_error)) {
        detail_ = snapshot_.has_value()
            ? "Refresh failed; retained generation " +
                std::to_string(snapshot_->generation) + ": " +
                refresh_error
            : refresh_error;
        healthy_ = snapshot_.has_value();
        error = refresh_error;
        return false;
    }

    return reload(error);
}

EngineServiceStatus EngineServiceCore::status() const
{
    EngineServiceStatus result;
    result.healthy = healthy_;
    result.detail = detail_.empty()
        ? (healthy_ ? "Ready" : "Package state unavailable.")
        : detail_;

    if (snapshot_.has_value()) {
        result.generation = snapshot_->generation;
        result.published_at_unix =
            snapshot_->published_at_unix;
        result.source_fingerprint =
            snapshot_->source_fingerprint;
        result.installed_count =
            snapshot_->installed.size();
        result.available_count =
            snapshot_->available.size();
        result.update_count = updates_.size();
    }

    return result;
}

std::vector<PackageRecord> EngineServiceCore::installed() const
{
    if (!snapshot_.has_value()) {
        return {};
    }

    std::vector<PackageRecord> result = snapshot_->installed;
    for (PackageRecord &package : result) {
        classify_package_role(package);
    }
    return result;
}

std::vector<PackageRecord> EngineServiceCore::updates() const
{
    return updates_;
}

const std::string &EngineServiceCore::database_path() const noexcept
{
    return store_.path();
}

std::optional<TransactionPlan> EngineServiceCore::plan(
    const TransactionRequest &request,
    const std::string_view target_architecture,
    std::string &error) const
{
    if (!snapshot_.has_value()) {
        error =
            "No package-state generation is available for planning.";
        return std::nullopt;
    }
    if (!healthy_) {
        error =
            "Package state is not healthy enough to create a new "
            "transaction plan: " + detail_;
        return std::nullopt;
    }

    const std::string architecture =
        target_architecture.empty()
            ? native_debian_architecture()
            : std::string(target_architecture);
    if (architecture.empty()) {
        error =
            "Unable to determine the native Debian architecture.";
        return std::nullopt;
    }

    return DebianTransactionPlanner::plan(
        request,
        snapshot_->installed,
        snapshot_->available,
        architecture,
        snapshot_->generation,
        snapshot_->source_fingerprint,
        policy_,
        error);
}

std::string default_package_state_path()
{
    const char *override_path =
        std::getenv("INFILTRATOR_SOFTWARE_STATE_DB");
    if (override_path != nullptr &&
        *override_path != '\0') {
        return override_path;
    }

    if (geteuid() == 0) {
        return "/var/lib/infiltrator/software/packages.db";
    }

    const char *xdg_state = std::getenv("XDG_STATE_HOME");
    if (xdg_state != nullptr && *xdg_state != '\0') {
        return (
            std::filesystem::path(xdg_state) /
            "infiltrator/software/packages.db").string();
    }

    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return (
            std::filesystem::path(home) /
            ".local/state/infiltrator/software/packages.db").string();
    }

    return (
        std::filesystem::temp_directory_path() /
        ("infiltrator-software-" + std::to_string(getuid())) /
        "packages.db").string();
}

std::string native_debian_architecture()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "amd64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "i386";
#elif defined(__arm__)
    return "armhf";
#elif defined(__powerpc64__) && defined(__LITTLE_ENDIAN__)
    return "ppc64el";
#elif defined(__s390x__)
    return "s390x";
#elif defined(__riscv) && (__riscv_xlen == 64)
    return "riscv64";
#else
    return {};
#endif
}

} // namespace infiltrator::software
