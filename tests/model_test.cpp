// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/model.hpp"

#include <cassert>

int main()
{
    using namespace infiltrator::software;

    PackageRecord package;
    assert(!valid_identity(package));

    package.id = "infiltrator-calculator";
    package.name = "Calculator";
    package.channel = Channel::stable;
    package.kind = PackageKind::application;
    assert(valid_identity(package));
    assert(channel_name(package.channel) == "Stable");
    assert(package_kind_name(package.kind) == "Application");
    assert(transaction_action_name(TransactionAction::upgrade) == "Upgrade");

    TransactionPlan plan;
    plan.items.push_back(TransactionItem{
        package.id, TransactionAction::upgrade, "0.1.36", "0.1.37",
        4096, 2048, false});
    plan.download_bytes = 2048;
    plan.disk_delta_bytes = 4096;
    assert(plan.items.size() == 1U);
    assert(!plan.touches_system);
    return 0;
}
