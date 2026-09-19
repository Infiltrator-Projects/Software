// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_APT_BACKEND_HPP
#define INFILTRATOR_SOFTWARE_APT_BACKEND_HPP

#include "backend/package_backend.hpp"

namespace infiltrator::software {

class AptBackend final : public PackageBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool available() const noexcept override;
    [[nodiscard]] BackendCapabilities capabilities() const noexcept override;

    std::vector<PackageRecord> list_installed(std::string &error) override;
    std::vector<PackageRecord> search(
        std::string_view query, std::string &error) override;
    std::vector<PackageRecord> list_updates(std::string &error) override;
    std::optional<TransactionPlan> plan(
        const TransactionRequest &request, std::string &error) override;
};

} // namespace infiltrator::software
#endif
