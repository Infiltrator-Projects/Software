// SPDX-License-Identifier: GPL-3.0-or-later
#include "backends/apt/apt_backend.hpp"

#include <cassert>
#include <string>

int main()
{
    using namespace infiltrator::software;

    AptBackend backend;
    assert(backend.name() == "APT/.deb");

    if (!backend.available()) {
        return 0;
    }

    const BackendCapabilities caps = backend.capabilities();
    assert(caps.installed_inventory);
    assert(caps.transaction_planning == caps.update_inventory);
    assert(!caps.transaction_execution);

    std::string error;
    const auto installed = backend.list_installed(error);
    assert(error.empty());
    assert(!installed.empty());

    for (const auto &package : installed) {
        assert(valid_identity(package));
        assert(package.state == InstallState::installed);
    }

    if (caps.update_inventory) {
        error.clear();
        const auto updates = backend.list_updates(error);
        assert(error.empty());
        for (const auto &package : updates) {
            assert(valid_identity(package));
            assert(package.state == InstallState::upgradable);
            assert(!package.installed_version.empty());
            assert(!package.available_version.empty());
            assert(package.installed_version != package.available_version);
        }
    }

    error.clear();
    const auto plan = backend.plan(TransactionRequest{}, error);
    assert(!plan.has_value());
    assert(!error.empty());
    return 0;
}
