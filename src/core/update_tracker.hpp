// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_UPDATE_TRACKER_HPP
#define INFILTRATOR_SOFTWARE_UPDATE_TRACKER_HPP

#include "core/model.hpp"
#include "core/update_policy.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace infiltrator::software {

struct UpdateNotificationResult {
    bool notify{false};
    std::size_t relevant_updates{0U};
    unsigned oldest_age_days{0U};
    unsigned maximum_seen_days{0U};
};

std::string update_tracker_path();

bool evaluate_update_notification(
    const std::vector<PackageRecord> &updates,
    const SoftwarePreferences &preferences,
    std::int64_t now_unix,
    std::int64_t last_successful_update_unix,
    const std::string &path,
    UpdateNotificationResult &result,
    std::string &error);

} // namespace infiltrator::software

#endif
