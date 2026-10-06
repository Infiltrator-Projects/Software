// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/system_data.hpp"

#include "app/installed_inventory.hpp"
#include "client/engine_client.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace infiltrator::software::app {

SystemDataResult load_system_data(const bool refresh_metadata)
{
    SystemDataResult result;

    std::vector<PackageRecord> installed =
        infiltrator::software::read_installed_packages(
            result.error,
            &result.from_engine);
    if (result.error.empty()) {
        for (PackageRecord &package : installed) {
            infiltrator::software::classify_package_role(package);
            if (infiltrator::software::is_system_component(package)) {
                result.components.emplace_back(std::move(package));
            }
        }
    }

    infiltrator::software::EngineClient engine;
    if (refresh_metadata) {
        if (!engine.refresh(result.update_warning)) {
            result.update_warning =
                "Update refresh failed: " + result.update_warning;
        }
    }

    std::vector<PackageRecord> updates;
    std::string update_error;
    if (result.update_warning.empty() &&
        !engine.list_updates(updates, update_error) &&
        !refresh_metadata) {
        std::string refresh_error;
        if (engine.refresh(refresh_error)) {
            update_error.clear();
            (void)engine.list_updates(updates, update_error);
        } else {
            update_error =
                "Native update state unavailable: " + refresh_error;
        }
    }
    if (!update_error.empty()) {
        result.update_warning = update_error;
    }

    for (PackageRecord &package : updates) {
        infiltrator::software::classify_package_role(package);
        if (infiltrator::software::is_system_component(package)) {
            result.updates.emplace_back(std::move(package));
        }
    }

    auto rank = [](const PackageRecord &package) {
        switch (package.kind) {
        case infiltrator::software::PackageKind::kernel:
            return 0;
        case infiltrator::software::PackageKind::driver:
            return 1;
        case infiltrator::software::PackageKind::system:
            return 2;
        default:
            return 3;
        }
    };
    std::stable_sort(
        result.components.begin(),
        result.components.end(),
        [&](const PackageRecord &left, const PackageRecord &right) {
            const int left_rank = rank(left);
            const int right_rank = rank(right);
            if (left_rank != right_rank) {
                return left_rank < right_rank;
            }
            return left.name < right.name;
        });

    return result;
}

} // namespace infiltrator::software::app
