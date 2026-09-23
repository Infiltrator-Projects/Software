// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_ENGINE_CLIENT_HPP
#define INFILTRATOR_SOFTWARE_ENGINE_CLIENT_HPP

#include "core/model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace infiltrator::software {

class EngineClient final {
public:
    bool list_installed(
        std::vector<PackageRecord> &packages,
        std::string &error) const;

    bool list_updates(
        std::vector<PackageRecord> &packages,
        std::string &error) const;

    std::optional<TransactionPlan> plan(
        const TransactionRequest &request,
        std::string &error) const;

    bool reload(std::string &error) const;
    bool refresh(std::string &error) const;
};

} // namespace infiltrator::software

#endif
