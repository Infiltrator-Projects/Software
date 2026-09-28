// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/installed_inventory.hpp"

#include "backends/apt/apt_backend.hpp"
#include "client/engine_client.hpp"
#include "core/update_policy.hpp"

namespace infiltrator::software {

std::vector<PackageRecord> read_installed_packages(
    std::string &error,
    bool *from_engine)
{
    if (from_engine != nullptr) {
        *from_engine = false;
    }

    EngineClient engine;
    std::vector<PackageRecord> packages;
    std::string engine_error;
    if (engine.list_installed(packages, engine_error)) {
        for (PackageRecord &package : packages) {
            classify_package_role(package);
        }
        if (from_engine != nullptr) {
            *from_engine = true;
        }
        error.clear();
        return packages;
    }

    AptBackend fallback;
    std::string fallback_error;
    packages = fallback.list_installed(fallback_error);
    if (fallback_error.empty()) {
        for (PackageRecord &package : packages) {
            classify_package_role(package);
        }
        error.clear();
        return packages;
    }

    error = fallback_error;
    if (!engine_error.empty()) {
        error =
            "Shared engine unavailable: " + engine_error +
            " Direct Debian-state fallback failed: " +
            fallback_error;
    }
    return {};
}

} // namespace infiltrator::software
