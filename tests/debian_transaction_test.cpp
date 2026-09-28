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
    assert(conflict_plan.has_value());
    assert(error.empty());
    assert(conflict_plan->items.size() == 2U);
    const TransactionItem *new_tool_item =
        find_item(*conflict_plan, "new-tool");
    const TransactionItem *old_tool_item =
        find_item(*conflict_plan, "old-tool");
    assert(new_tool_item != nullptr);
    assert(old_tool_item != nullptr);
    assert(new_tool_item->action == TransactionAction::install);
    assert(old_tool_item->action == TransactionAction::remove);
    assert(old_tool_item->from_version == "1.5");

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

    PackageRecord removable_app =
        installed("app", "1.0", 1500U);
    removable_app.depends = "libcore (>= 1.0)";
    PackageRecord removable_library =
        installed("libcore", "1.0", 800U);

    TransactionRequest remove_request;
    remove_request.action = TransactionAction::remove;
    remove_request.package_ids = {"app"};

    error.clear();
    const auto remove_plan =
        DebianTransactionPlanner::plan(
            remove_request,
            {removable_app, removable_library},
            repository,
            "amd64",
            10U,
            "snapshot-10",
            policy,
            error);
    assert(remove_plan.has_value());
    assert(error.empty());
    assert(remove_plan->items.size() == 1U);
    assert(remove_plan->items.front().action == TransactionAction::remove);
    assert(remove_plan->items.front().from_version == "1.0");
    assert(remove_plan->items.front().to_version.empty());
    assert(remove_plan->disk_delta_bytes == -1500);

    TransactionRequest break_dependency;
    break_dependency.action = TransactionAction::remove;
    break_dependency.package_ids = {"libcore"};

    error.clear();
    const auto unsafe_remove =
        DebianTransactionPlanner::plan(
            break_dependency,
            {removable_app, removable_library},
            repository,
            "amd64",
            11U,
            "snapshot-11",
            policy,
            error);
    assert(!unsafe_remove.has_value());
    assert(error.find("app") != std::string::npos);
    assert(error.find("libcore") != std::string::npos);

    TransactionRequest remove_together;
    remove_together.action = TransactionAction::remove;
    remove_together.package_ids = {"app", "libcore"};

    error.clear();
    const auto joint_remove =
        DebianTransactionPlanner::plan(
            remove_together,
            {removable_app, removable_library},
            repository,
            "amd64",
            12U,
            "snapshot-12",
            policy,
            error);
    assert(joint_remove.has_value());
    assert(joint_remove->items.size() == 2U);
    assert(joint_remove->disk_delta_bytes == -2300);

    PackageRecord essential =
        installed("essential-base", "1.0", 100U);
    essential.essential = true;

    TransactionRequest essential_remove;
    essential_remove.action = TransactionAction::remove;
    essential_remove.package_ids = {"essential-base"};

    error.clear();
    const auto essential_plan =
        DebianTransactionPlanner::plan(
            essential_remove,
            {essential},
            {},
            "amd64",
            13U,
            "snapshot-13",
            policy,
            error);
    assert(!essential_plan.has_value());
    assert(error.find("Essential") != std::string::npos);

    DebianPackageVersion newer_pinned =
        available("pinned-app", "2.0", 20U, 200U, "newer");
    newer_pinned.pin_priority = 100;
    DebianPackageVersion preferred_pinned =
        available("pinned-app", "1.0", 10U, 100U, "preferred");
    preferred_pinned.pin_priority = 700;

    TransactionRequest pinned_install;
    pinned_install.action = TransactionAction::install;
    pinned_install.package_ids = {"pinned-app"};
    DebianCandidatePolicy pinned_policy;
    error.clear();
    const auto pinned_plan =
        DebianTransactionPlanner::plan(
            pinned_install, {},
            {newer_pinned, preferred_pinned},
            "amd64", 14U, "snapshot-14",
            pinned_policy, error);
    assert(pinned_plan.has_value());
    assert(error.empty());
    const TransactionItem *pinned_item =
        find_item(*pinned_plan, "pinned-app");
    assert(pinned_item != nullptr);
    assert(pinned_item->to_version == "1.0");

    DebianPackageVersion software_replacement =
        available(
            "infiltrator-software",
            "0.3.53",
            50U,
            500U);
    software_replacement.provides = "mintupdate";
    software_replacement.conflicts = "mintupdate";
    software_replacement.replaces = "mintupdate";

    PackageRecord mint_meta =
        installed("mint-meta-cinnamon", "2026.1", 10U);
    mint_meta.depends = "mintupdate";

    TransactionRequest replacement_request;
    replacement_request.action = TransactionAction::upgrade;
    replacement_request.package_ids = {"infiltrator-software"};

    error.clear();
    const auto replacement_plan =
        DebianTransactionPlanner::plan(
            replacement_request,
            {
                installed(
                    "infiltrator-software",
                    "0.3.49",
                    400U),
                installed("mintupdate", "7.1.4", 200U),
                mint_meta
            },
            {software_replacement},
            "amd64",
            15U,
            "snapshot-15",
            policy,
            error);
    assert(replacement_plan.has_value());
    assert(error.empty());
    const TransactionItem *software_item =
        find_item(
            *replacement_plan,
            "infiltrator-software");
    const TransactionItem *mintupdate_item =
        find_item(*replacement_plan, "mintupdate");
    assert(software_item != nullptr);
    assert(
        software_item->action ==
        TransactionAction::upgrade);
    assert(mintupdate_item != nullptr);
    assert(
        mintupdate_item->action ==
        TransactionAction::remove);
    assert(mintupdate_item->from_version == "7.1.4");
    assert(!mintupdate_item->requested);

    DebianPackageVersion recommended_app =
        available(
            "recommended-app",
            "1.0",
            20U,
            200U);
    recommended_app.recommends =
        "recommended-addon";
    DebianPackageVersion recommended_addon =
        available(
            "recommended-addon",
            "1.0",
            5U,
            50U);

    TransactionRequest recommends_request;
    recommends_request.action =
        TransactionAction::install;
    recommends_request.package_ids =
        {"recommended-app"};
    recommends_request.install_recommends = true;

    error.clear();
    const auto recommends_plan =
        DebianTransactionPlanner::plan(
            recommends_request,
            {},
            {recommended_app, recommended_addon},
            "amd64",
            16U,
            "snapshot-16",
            policy,
            error);
    assert(recommends_plan.has_value());
    assert(error.empty());
    assert(
        find_item(
            *recommends_plan,
            "recommended-app") != nullptr);
    assert(
        find_item(
            *recommends_plan,
            "recommended-addon") != nullptr);

    /*
     * A mixed transaction must be solved as one projected final state.
     * Removing old-provider is safe only because new-provider is selected in
     * the same dependency solution and satisfies consumer's alternative.
     */
    PackageRecord consumer =
        installed("consumer", "1.0", 100U);
    consumer.depends =
        "old-provider | new-provider";
    PackageRecord old_provider =
        installed("old-provider", "1.0", 50U);
    DebianPackageVersion new_provider =
        available(
            "new-provider",
            "2.0",
            10U,
            60U);

    TransactionRequest mixed_request;
    mixed_request.action =
        TransactionAction::install;
    mixed_request.package_ids =
        {"new-provider"};
    mixed_request.remove_package_ids =
        {"old-provider"};

    error.clear();
    const auto mixed_plan =
        DebianTransactionPlanner::plan(
            mixed_request,
            {consumer, old_provider},
            {new_provider},
            "amd64",
            17U,
            "snapshot-17",
            policy,
            error);
    assert(mixed_plan.has_value());
    assert(error.empty());
    assert(mixed_plan->items.size() == 2U);
    const TransactionItem *old_provider_item =
        find_item(*mixed_plan, "old-provider");
    const TransactionItem *new_provider_item =
        find_item(*mixed_plan, "new-provider");
    assert(old_provider_item != nullptr);
    assert(
        old_provider_item->action ==
        TransactionAction::remove);
    assert(old_provider_item->requested);
    assert(new_provider_item != nullptr);
    assert(
        new_provider_item->action ==
        TransactionAction::install);
    assert(new_provider_item->requested);

    DebianPackageVersion unrelated =
        available(
            "unrelated",
            "1.0",
            5U,
            5U);
    TransactionRequest broken_mixed;
    broken_mixed.action =
        TransactionAction::install;
    broken_mixed.package_ids =
        {"unrelated"};
    broken_mixed.remove_package_ids =
        {"old-provider"};

    error.clear();
    const auto broken_mixed_plan =
        DebianTransactionPlanner::plan(
            broken_mixed,
            {consumer, old_provider},
            {unrelated},
            "amd64",
            18U,
            "snapshot-18",
            policy,
            error);
    assert(!broken_mixed_plan.has_value());
    assert(error.find("consumer") != std::string::npos);

    TransactionRequest contradictory;
    contradictory.action =
        TransactionAction::install;
    contradictory.package_ids =
        {"old-provider"};
    contradictory.remove_package_ids =
        {"old-provider"};
    DebianPackageVersion old_provider_available =
        available(
            "old-provider",
            "2.0",
            10U,
            55U);

    error.clear();
    const auto contradictory_plan =
        DebianTransactionPlanner::plan(
            contradictory,
            {old_provider},
            {old_provider_available},
            "amd64",
            19U,
            "snapshot-19",
            policy,
            error);
    assert(!contradictory_plan.has_value());
    assert(
        error.find("cannot explicitly remove") !=
        std::string::npos);

    return 0;
}
