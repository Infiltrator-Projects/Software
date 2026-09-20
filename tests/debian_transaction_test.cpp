// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_transaction.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using infiltrator::software::DebianPackageVersion;
using infiltrator::software::InstallState;
using infiltrator::software::PackageRecord;
using infiltrator::software::TransactionAction;
using infiltrator::software::TransactionItem;
using infiltrator::software::TransactionPlan;

PackageRecord installed(
    const std::string &name,
    const std::string &version,
    const std::uint64_t size,
    const std::string &architecture = "amd64")
{
    PackageRecord package;
    package.id = name;
    package.name = name;
    package.package_name = name;
    package.architecture = architecture;
    package.installed_version = version;
    package.available_version = version;
    package.installed_size_bytes = size;
    package.state = InstallState::installed;
    return package;
}

DebianPackageVersion available(
    const std::string &name,
    const std::string &version,
    const std::uint64_t download,
    const std::uint64_t installed_size,
    const std::string &source = "stable",
    const std::string &architecture = "amd64")
{
    DebianPackageVersion package;
    package.package = name;
    package.version = version;
    package.architecture = architecture;
    package.source = source;
    package.filename =
        "pool/" + name + "_" + version + "_" +
        architecture + ".deb";
    package.sha256 = "0123456789abcdef";
    package.size_bytes = download;
    package.installed_size_bytes = installed_size;
    return package;
}

const TransactionItem *find_item(
    const TransactionPlan &plan,
    const std::string &prefix)
{
    const auto found =
        std::find_if(
            plan.items.begin(),
            plan.items.end(),
            [&](const TransactionItem &item) {
                return item.package_id.rfind(prefix, 0U) == 0U;
            });
    return found == plan.items.end() ? nullptr : &*found;
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    DebianPackageVersion app =
        available("app", "2.0", 100U, 2000U);
    app.depends = "libcore (>= 2.0)";

    DebianPackageVersion library =
        available("libcore", "2.0", 50U, 1000U);
    library.essential = true;

    const std::vector<PackageRecord> current{
        installed("app", "1.0", 1500U),
        installed("libcore", "1.0", 800U)
    };
    const std::vector<DebianPackageVersion> repository{
        app, library
    };

    DebianCandidatePolicy policy;
    policy.source_priorities["stable"] = 500;

    TransactionRequest upgrade;
    upgrade.action = TransactionAction::upgrade;
    upgrade.package_ids = {"app"};

    std::string error;
    const auto update_plan =
        DebianTransactionPlanner::plan(
            upgrade,
            current,
            repository,
            "amd64",
            42U,
            "snapshot-42",
            policy,
            error);

    assert(update_plan.has_value());
    assert(error.empty());
    assert(update_plan->state_generation == 42U);
    assert(update_plan->source_fingerprint == "snapshot-42");
    assert(update_plan->items.size() == 2U);
    assert(update_plan->download_bytes == 150U);
    assert(update_plan->disk_delta_bytes == 700);
    assert(update_plan->touches_system);

    const TransactionItem *app_item =
        find_item(*update_plan, "app");
    assert(app_item != nullptr);
    assert(app_item->action == TransactionAction::upgrade);
    assert(app_item->from_version == "1.0");
    assert(app_item->to_version == "2.0");
    assert(app_item->requested);
    assert(app_item->source == "stable");
    assert(!app_item->sha256.empty());

    const TransactionItem *library_item =
        find_item(*update_plan, "libcore");
    assert(library_item != nullptr);
    assert(library_item->action == TransactionAction::upgrade);
    assert(!library_item->requested);
    assert(library_item->system_critical);

    DebianPackageVersion tool =
        available("tool", "1.0", 30U, 500U);
    tool.depends = "helper";
    DebianPackageVersion helper =
        available("helper", "1.0", 10U, 100U);

    TransactionRequest install_request;
    install_request.action = TransactionAction::install;
    install_request.package_ids = {"tool"};

    error.clear();
    const auto install_plan =
        DebianTransactionPlanner::plan(
            install_request,
            {},
            {tool, helper},
            "amd64",
            7U,
            "snapshot-7",
            policy,
            error);
    assert(install_plan.has_value());
    assert(error.empty());
    assert(install_plan->items.size() == 2U);
    assert(install_plan->download_bytes == 40U);
    assert(install_plan->disk_delta_bytes == 600);
    assert(
        find_item(*install_plan, "tool")->action ==
        TransactionAction::install);
    assert(
        find_item(*install_plan, "helper")->action ==
        TransactionAction::install);

    DebianPackageVersion conflicting =
        available("new-tool", "2.0", 5U, 5U);
    conflicting.conflicts = "old-tool (<< 2.0)";

    TransactionRequest conflict_request;
    conflict_request.action = TransactionAction::install;
    conflict_request.package_ids = {"new-tool"};

    error.clear();
    const auto conflict_plan =
        DebianTransactionPlanner::plan(
            conflict_request,
            {installed("old-tool", "1.5", 10U)},
            {conflicting},
            "amd64",
            8U,
            "snapshot-8",
            policy,
            error);
    assert(!conflict_plan.has_value());
    assert(error.find("conflict") != std::string::npos);

    DebianCandidatePolicy held_policy = policy;
    held_policy.held_packages.insert("app");

    error.clear();
    const auto held_plan =
        DebianTransactionPlanner::plan(
            upgrade,
            current,
            repository,
            "amd64",
            9U,
            "snapshot-9",
            held_policy,
            error);
    assert(!held_plan.has_value());
    assert(error.find("held") != std::string::npos);

    TransactionRequest remove_request;
    remove_request.action = TransactionAction::remove;
    remove_request.package_ids = {"app"};

    error.clear();
    const auto remove_plan =
        DebianTransactionPlanner::plan(
            remove_request,
            current,
            repository,
            "amd64",
            10U,
            "snapshot-10",
            policy,
            error);
    assert(!remove_plan.has_value());
    assert(error.find("reverse-dependency") != std::string::npos);

    return 0;
}
