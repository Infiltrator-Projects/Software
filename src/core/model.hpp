// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_MODEL_HPP
#define INFILTRATOR_SOFTWARE_MODEL_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

enum class Channel { stable, beta, alpha, unknown };
enum class PackageKind { application, system, library, driver, kernel, runtime, unknown };
enum class InstallState { not_installed, installed, upgradable };
enum class TransactionAction { install, upgrade, remove };
enum class SourceKind { infiltrator, apt, flatpak };

struct PackageRecord {
    std::string id;
    std::string name;
    std::string package_name;
    std::string source_package;
    std::string publisher;
    std::string category;
    std::string architecture;
    std::string installed_version;
    std::string available_version;
    std::string summary;
    std::string description;
    std::string icon_name;
    std::string icon_url;
    std::string icon_sha256;
    std::string cached_icon_path;
    std::string source;
    std::string repository_origin;
    std::string repository_site;
    std::string policy_provider;
    std::string policy_reason;
    std::string selection_reason;
    int candidate_priority{0};
    std::string source_url;
    std::string release_url;
    std::string asset;
    std::string package_sha256;
    std::string published_at;
    Channel channel{Channel::unknown};
    PackageKind kind{PackageKind::unknown};
    InstallState state{InstallState::not_installed};
    std::uint64_t installed_size_bytes{0};
    std::uint64_t download_size_bytes{0};
    std::string depends;
    std::string pre_depends;
    std::string provides;
    std::string conflicts;
    std::string breaks;
    std::string replaces;
    std::string priority;
    std::string multi_arch;
    bool held{false};
    bool essential{false};
    bool protected_package{false};
    bool system_critical{false};
    bool security_update{false};
};

/*
 * KernelRecord is a client-visible domain snapshot, not an engine
 * implementation detail. Keep it in the product-neutral core model so GUI,
 * CLI and other clients do not need to include engine/private headers merely
 * to consume the public EngineClient contract.
 */
struct KernelRecord {
    std::string version;
    std::string package_version;
    std::string kernel_type;
    std::string series;
    std::string image_package;
    std::string origin;
    std::string archive;
    std::string support_status;
    std::string support_end;
    bool installed{false};
    bool active{false};
    bool installable{false};
    bool supported{false};
    bool superseded{false};
    bool end_of_life{false};
    bool safe_to_remove{false};
    std::vector<std::string> install_package_ids;
    std::vector<std::string> remove_package_ids;
};

/* Repository/source identity is product state consumed by the UI and engine. */
struct SourceRecord {
    SourceKind kind{SourceKind::apt};
    std::string name;
    std::string location;
    std::string detail;
    std::string scope;
    std::string backing_file;
    std::string apt_suites;
    std::size_t entry_index{0U};
    bool enabled{true};
};

struct TransactionRequest {
    TransactionAction action{TransactionAction::install};
    std::vector<std::string> package_ids;
    /*
     * Optional explicit removals that must be resolved in the same projected
     * final state as an install/upgrade request. Pure removal transactions
     * continue to use action=remove and package_ids.
     */
    std::vector<std::string> remove_package_ids;
    bool install_recommends{false};
};

struct TransactionItem {
    std::string package_id;
    TransactionAction action{TransactionAction::install};
    std::string from_version;
    std::string to_version;
    std::int64_t disk_delta_bytes{0};
    std::uint64_t download_bytes{0};
    bool system_critical{false};
    std::string architecture;
    std::string source;
    std::string filename;
    std::string sha256;
    bool requested{false};
};

struct TransactionPlan {
    std::vector<TransactionItem> items;
    std::uint64_t download_bytes{0};
    std::int64_t disk_delta_bytes{0};
    bool touches_system{false};
    std::uint64_t state_generation{0};
    std::string source_fingerprint;
};

std::string_view channel_name(Channel channel) noexcept;
std::string_view package_kind_name(PackageKind kind) noexcept;
std::string_view transaction_action_name(TransactionAction action) noexcept;
std::string_view source_kind_name(SourceKind kind) noexcept;
bool valid_identity(const PackageRecord &package) noexcept;
void classify_package_role(PackageRecord &package) noexcept;
bool is_system_component(const PackageRecord &package) noexcept;

} // namespace infiltrator::software
#endif
