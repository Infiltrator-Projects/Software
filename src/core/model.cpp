// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/model.hpp"

namespace infiltrator::software {

std::string_view channel_name(const Channel channel) noexcept
{
    switch (channel) {
    case Channel::stable: return "Stable";
    case Channel::beta: return "Beta";
    case Channel::alpha: return "Alpha";
    case Channel::unknown: return "Unknown";
    }
    return "Unknown";
}

std::string_view package_kind_name(const PackageKind kind) noexcept
{
    switch (kind) {
    case PackageKind::application: return "Application";
    case PackageKind::system: return "System";
    case PackageKind::library: return "Library";
    case PackageKind::driver: return "Driver";
    case PackageKind::kernel: return "Kernel";
    case PackageKind::runtime: return "Runtime";
    case PackageKind::unknown: return "Unknown";
    }
    return "Unknown";
}

std::string_view transaction_action_name(const TransactionAction action) noexcept
{
    switch (action) {
    case TransactionAction::install: return "Install";
    case TransactionAction::upgrade: return "Upgrade";
    case TransactionAction::remove: return "Remove";
    }
    return "Unknown";
}

bool valid_identity(const PackageRecord &package) noexcept
{
    return !package.id.empty() && !package.name.empty();
}

} // namespace infiltrator::software
