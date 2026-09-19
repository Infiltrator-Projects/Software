// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_MODEL_HPP
#define INFILTRATOR_SOFTWARE_MODEL_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

enum class Channel { stable, beta, alpha, unknown };
enum class PackageKind { application, system, library, driver, kernel, runtime, unknown };
enum class InstallState { not_installed, installed, upgradable };
enum class TransactionAction { install, upgrade, remove };

struct PackageRecord {
    std::string id;
    std::string name;
    std::string publisher;
    std::string installed_version;
    std::string available_version;
    std::string summary;
    std::string description;
    std::string icon_name;
    std::string source;
    Channel channel{Channel::unknown};
    PackageKind kind{PackageKind::unknown};
    InstallState state{InstallState::not_installed};
    std::uint64_t installed_size_bytes{0};
    std::uint64_t download_size_bytes{0};
    bool system_critical{false};
};

struct TransactionRequest {
    TransactionAction action{TransactionAction::install};
    std::vector<std::string> package_ids;
};

struct TransactionItem {
    std::string package_id;
    TransactionAction action{TransactionAction::install};
    std::string from_version;
    std::string to_version;
    std::int64_t disk_delta_bytes{0};
    std::uint64_t download_bytes{0};
    bool system_critical{false};
};

struct TransactionPlan {
    std::vector<TransactionItem> items;
    std::uint64_t download_bytes{0};
    std::int64_t disk_delta_bytes{0};
    bool touches_system{false};
};

std::string_view channel_name(Channel channel) noexcept;
std::string_view package_kind_name(PackageKind kind) noexcept;
std::string_view transaction_action_name(TransactionAction action) noexcept;
bool valid_identity(const PackageRecord &package) noexcept;

} // namespace infiltrator::software
#endif
