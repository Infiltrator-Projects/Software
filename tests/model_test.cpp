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

    PackageRecord kernel;
    kernel.id = "linux-image-7.0.0-31-generic";
    kernel.name = kernel.id;
    kernel.package_name = kernel.id;
    classify_package_role(kernel);
    assert(kernel.kind == PackageKind::kernel);
    assert(kernel.system_critical);
    assert(is_system_component(kernel));

    PackageRecord driver;
    driver.id = "nvidia-driver-550";
    driver.name = driver.id;
    driver.package_name = driver.id;
    classify_package_role(driver);
    assert(driver.kind == PackageKind::driver);
    assert(is_system_component(driver));

    PackageRecord ordinary;
    ordinary.id = "infiltrator-calendar";
    ordinary.name = ordinary.id;
    ordinary.package_name = ordinary.id;
    classify_package_role(ordinary);
    assert(ordinary.kind == PackageKind::application);
    assert(!is_system_component(ordinary));

    TransactionItem item;
    item.package_id = package.id;
    item.action = TransactionAction::upgrade;
    item.from_version = "0.1.36";
    item.to_version = "0.1.37";
    item.disk_delta_bytes = 4096;
    item.download_bytes = 2048;

    TransactionPlan plan;
    plan.items.push_back(item);
    plan.download_bytes = 2048;
    plan.disk_delta_bytes = 4096;
    assert(plan.items.size() == 1U);
    assert(!plan.touches_system);
    return 0;
}
