// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_PACKAGE_BACKEND_HPP
#define INFILTRATOR_SOFTWARE_PACKAGE_BACKEND_HPP

#include "core/model.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct BackendCapabilities {
    bool installed_inventory{false};
    bool catalogue_search{false};
    bool update_inventory{false};
    bool transaction_planning{false};
    bool transaction_execution{false};
    bool repository_management{false};
    bool repair{false};
};

class PackageBackend {
public:
    virtual ~PackageBackend() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool available() const noexcept = 0;
    [[nodiscard]] virtual BackendCapabilities capabilities() const noexcept = 0;

    virtual std::vector<PackageRecord> list_installed(std::string &error) = 0;
    virtual std::vector<PackageRecord> search(
        std::string_view query, std::string &error) = 0;
    virtual std::vector<PackageRecord> list_updates(std::string &error) = 0;
    virtual std::optional<TransactionPlan> plan(
        const TransactionRequest &request, std::string &error) = 0;
};

} // namespace infiltrator::software
#endif
