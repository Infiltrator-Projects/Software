// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_UPDATES_CONTROLLER_HPP
#define INFILTRATOR_SOFTWARE_UPDATES_CONTROLLER_HPP

#include "core/model.hpp"
#include "external/external_updates.hpp"

#include <optional>
#include <string>
#include <vector>

namespace infiltrator::software::app {

struct UpdatesRefreshRequest {
    unsigned int generation{0U};
    bool refresh_metadata{false};
    bool refresh_external{true};
    bool show_flatpak{true};
    bool show_cinnamon{true};
};

struct UpdatesRefreshResult {
    unsigned int generation{0U};
    bool refreshed_metadata{false};
    bool external_refreshed{false};
    std::vector<PackageRecord> records;
    std::vector<ExternalUpdate> external_records;
    std::string external_error;
    std::string error;
};

struct UpdatePlanRequest {
    std::vector<std::string> package_ids;
    bool install_recommends{false};
};

struct UpdatePlanResult {
    std::optional<TransactionPlan> plan;
    std::string error;
};

/*
 * The Updates page owns presentation and GTK lifecycle only. These operations
 * own package-engine reconciliation, external update discovery and transaction
 * planning so main.cpp does not need to know how update state is assembled.
 */
UpdatesRefreshResult refresh_updates_data(
    const UpdatesRefreshRequest &request);

UpdatePlanResult plan_updates(
    const UpdatePlanRequest &request);

} // namespace infiltrator::software::app

#endif
