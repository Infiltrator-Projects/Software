// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/installed_inventory.hpp"

#include "client/engine_client.hpp"
#include "core/update_policy.hpp"

namespace infiltrator::software {

std::vector<PackageRecord> read_installed_packages(
    std::string &error)
{
    EngineClient engine;
    std::vector<PackageRecord> packages;
    if (!engine.list_installed(packages, error)) {
        return {};
    }

    for (PackageRecord &package : packages) {
        classify_package_role(package);
    }
    error.clear();
    return packages;
}

} // namespace infiltrator::software
