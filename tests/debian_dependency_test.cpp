// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_dependency.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace {

using infiltrator::software::DebianPackageVersion;
using infiltrator::software::InstallState;
using infiltrator::software::PackageRecord;

DebianPackageVersion package(
    const std::string &name,
    const std::string &version,
    const std::string &source = "stable",
    const std::string &architecture = "amd64")
{
    DebianPackageVersion result;
    result.package = name;
    result.version = version;
    result.source = source;
    result.architecture = architecture;
    result.filename =
        "pool/" + name + "_" + version + "_" + architecture + ".deb";
    return result;
}

PackageRecord installed(
    const std::string &name,
    const std::string &version,
    const std::string &architecture = "amd64")
{
    PackageRecord result;
    result.id = name;
    result.name = name;
    result.package_name = name;
    result.installed_version = version;
    result.available_version = version;
    result.architecture = architecture;
    result.state = InstallState::installed;
    return result;
}

bool contains(
    const std::vector<DebianPackageVersion> &packages,
    const std::string &name)
{
    return std::any_of(
        packages.begin(),
        packages.end(),
        [&](const DebianPackageVersion &package_version) {
            return package_version.package == name;
        });
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    std::string error;
    const auto expression = DebianDependencyResolver::parse(
        "libc6 (>= 2.38), crypto-api (= 3.0) | libssl3 (>= 3.0), helper:any",
        error);
    assert(expression.has_value());
    assert(error.empty());
    assert(expression->groups.size() == 3U);
    assert(expression->groups[0].alternatives.size() == 1U);
    assert(expression->groups[1].alternatives.size() == 2U);
    assert(expression->groups[2].alternatives[0].package == "helper");
    assert(
        expression->groups[2].alternatives[0].architecture_qualifier ==
        "any");

    DebianPackageVersion app = package("app", "2.0");
    app.pre_depends = "init-core (>= 1.0)";
    app.depends =
        "libc6 (>= 2.38), crypto-api (= 3.0) | libssl3 (>= 3.0), "
        "helper:any, cycle-a";

    DebianPackageVersion provider = package("libcrypto3", "3.2");
    provider.provides = "crypto-api (= 3.0)";

    DebianPackageVersion helper =
        package("helper", "1.0", "stable", "arm64");
    helper.multi_arch = "allowed";

    DebianPackageVersion cycle_a = package("cycle-a", "1.0");
    cycle_a.depends = "cycle-b";
    DebianPackageVersion cycle_b = package("cycle-b", "1.0");
    cycle_b.depends = "cycle-a";

    const std::vector<DebianPackageVersion> available{
        package("init-core", "1.1"),
        provider,
        package("libssl3", "3.1"),
        helper,
        cycle_a,
        cycle_b
    };

    const std::vector<PackageRecord> current{
        installed("libc6", "2.39")
    };

    DebianCandidatePolicy policy;
    policy.source_priorities["stable"] = 500;

    const DebianResolution resolved =
        DebianDependencyResolver::resolve(
            {app}, current, available, "amd64", policy);

    assert(resolved.complete());
    assert(contains(resolved.selected, "app"));
    assert(contains(resolved.selected, "init-core"));
    assert(contains(resolved.selected, "libcrypto3"));
    assert(!contains(resolved.selected, "libssl3"));
    assert(contains(resolved.selected, "helper"));
    assert(contains(resolved.selected, "cycle-a"));
    assert(contains(resolved.selected, "cycle-b"));
    assert(!contains(resolved.selected, "libc6"));

    DebianPackageVersion broken = package("broken", "1.0");
    broken.depends = "missing-lib (>= 9.0) | held-lib";

    DebianCandidatePolicy held_policy;
    held_policy.held_packages.insert("held-lib");
    const DebianResolution unresolved =
        DebianDependencyResolver::resolve(
            {broken},
            {},
            {package("held-lib", "10.0")},
            "amd64",
            held_policy);
    assert(!unresolved.complete());
    assert(unresolved.problems.size() == 1U);
    assert(unresolved.problems[0].package == "broken");

    DebianPackageVersion alternative_owner =
        package("alternative-owner", "1.0");
    alternative_owner.depends =
        "dangerous-choice | safe-choice";
    DebianPackageVersion dangerous_choice =
        package("dangerous-choice", "1.0");
    dangerous_choice.conflicts =
        "protected-runtime";
    DebianPackageVersion safe_choice =
        package("safe-choice", "1.0");
    PackageRecord protected_runtime =
        installed("protected-runtime", "1.0");
    protected_runtime.essential = true;

    const DebianResolution backtracked =
        DebianDependencyResolver::resolve(
            {alternative_owner},
            {protected_runtime},
            {dangerous_choice, safe_choice},
            "amd64",
            {});
    assert(backtracked.complete());
    assert(!contains(
        backtracked.selected,
        "dangerous-choice"));
    assert(contains(
        backtracked.selected,
        "safe-choice"));
    assert(backtracked.remove_installed.empty());

    DebianPackageVersion transitive_owner =
        package("transitive-owner", "1.0");
    transitive_owner.depends =
        "first-branch | second-branch";
    DebianPackageVersion first_branch =
        package("first-branch", "1.0");
    first_branch.depends =
        "missing-transitive (>= 9.0)";
    DebianPackageVersion second_branch =
        package("second-branch", "1.0");
    second_branch.depends =
        "available-transitive";
    const DebianResolution transitive_backtrack =
        DebianDependencyResolver::resolve(
            {transitive_owner},
            {},
            {
                first_branch,
                second_branch,
                package("available-transitive", "1.0")
            },
            "amd64",
            {});
    assert(transitive_backtrack.complete());
    assert(!contains(
        transitive_backtrack.selected,
        "first-branch"));
    assert(contains(
        transitive_backtrack.selected,
        "second-branch"));
    assert(contains(
        transitive_backtrack.selected,
        "available-transitive"));

    DebianPackageVersion conflict = package("new-tool", "2.0");
    conflict.conflicts = "old-tool (<< 2.0)";
    const DebianResolution conflicting =
        DebianDependencyResolver::resolve(
            {conflict},
            {installed("old-tool", "1.5")},
            {},
            "amd64",
            {});
    assert(conflicting.complete());
    assert(conflicting.remove_installed.size() == 1U);
    assert(conflicting.remove_installed.front() == "old-tool");

    DebianPackageVersion breaking = package("new-suite", "3.0");
    breaking.breaks = "old-tool (<< 2.0)";
    const DebianResolution repaired_break =
        DebianDependencyResolver::resolve(
            {breaking},
            {installed("old-tool", "1.5")},
            {package("old-tool", "2.1")},
            "amd64",
            {});
    assert(repaired_break.complete());
    assert(repaired_break.remove_installed.empty());
    assert(contains(repaired_break.selected, "old-tool"));
    const auto repaired_old_tool = std::find_if(
        repaired_break.selected.begin(),
        repaired_break.selected.end(),
        [](const DebianPackageVersion &candidate) {
            return candidate.package == "old-tool";
        });
    assert(repaired_old_tool != repaired_break.selected.end());
    assert(repaired_old_tool->version == "2.1");

    PackageRecord old_tool = installed("old-tool", "1.5");
    PackageRecord old_tool_consumer =
        installed("old-tool-consumer", "1.0");
    old_tool_consumer.depends = "old-tool (= 1.5)";
    const DebianResolution unsafe_conflict_removal =
        DebianDependencyResolver::resolve(
            {conflict},
            {old_tool, old_tool_consumer},
            {},
            "amd64",
            {});
    assert(!unsafe_conflict_removal.complete());

    DebianPackageVersion replacement =
        package("infiltrator-software", "0.3.53");
    replacement.provides = "mintupdate";
    replacement.conflicts = "mintupdate";
    replacement.replaces = "mintupdate";
    PackageRecord mint_meta =
        installed("mint-meta-cinnamon", "2026.1");
    mint_meta.depends = "mintupdate";
    const DebianResolution replacement_resolution =
        DebianDependencyResolver::resolve(
            {replacement},
            {installed("mintupdate", "7.1.4"), mint_meta},
            {}, "amd64", {});
    assert(replacement_resolution.complete());
    assert(replacement_resolution.remove_installed.size() == 1U);
    assert(replacement_resolution.remove_installed.front() == "mintupdate");

    DebianPackageVersion pinned_root =
        package("pinned-root", "1.0");
    pinned_root.depends = "libpin";
    DebianPackageVersion newer_pin =
        package("libpin", "2.0", "newer");
    newer_pin.pin_priority = 100;
    DebianPackageVersion preferred_pin =
        package("libpin", "1.0", "preferred");
    preferred_pin.pin_priority = 700;
    const DebianResolution pinned =
        DebianDependencyResolver::resolve(
            {pinned_root}, {}, {newer_pin, preferred_pin},
            "amd64", {});
    assert(pinned.complete());
    const auto pinned_choice = std::find_if(
        pinned.selected.begin(), pinned.selected.end(),
        [](const DebianPackageVersion &candidate) {
            return candidate.package == "libpin";
        });
    assert(pinned_choice != pinned.selected.end());
    assert(pinned_choice->version == "1.0");

    DebianPackageVersion requires_one =
        package("requires-one", "1.0");
    requires_one.depends = "shared (= 1.0)";
    DebianPackageVersion requires_two =
        package("requires-two", "1.0");
    requires_two.depends = "shared (= 2.0)";
    const DebianResolution incompatible =
        DebianDependencyResolver::resolve(
            {requires_one, requires_two}, {},
            {package("shared", "1.0"), package("shared", "2.0")},
            "amd64", {});
    assert(!incompatible.complete());

    DebianPackageVersion requires_installed =
        package("requires-installed", "1.0");
    requires_installed.depends =
        "shared-replaced (= 1.0)";
    const DebianResolution replaced_installed =
        DebianDependencyResolver::resolve(
            {requires_installed, package("shared-replaced", "2.0")},
            {installed("shared-replaced", "1.0")},
            {}, "amd64", {});
    assert(!replaced_installed.complete());

    /*
     * Installed packages can satisfy dependencies through Provides rather than
     * their concrete package name.  This is how Ubuntu satisfies dependencies
     * such as accountsservice -> default-dbus-system-bus | dbus-system-bus.
     * Final-state validation must honour the provider recorded by dpkg.
     */
    PackageRecord dbus_provider =
        installed("dbus-daemon", "1.14.10");
    dbus_provider.provides =
        "default-dbus-system-bus, dbus-system-bus";
    PackageRecord accountsservice =
        installed("accountsservice", "23.13.9");
    accountsservice.depends =
        "default-dbus-system-bus | dbus-system-bus";

    const DebianResolution installed_virtual_provider =
        DebianDependencyResolver::resolve(
            {package("unrelated-update", "2.0")},
            {accountsservice, dbus_provider},
            {}, "amd64", {});
    assert(installed_virtual_provider.complete());

    PackageRecord versioned_provider =
        installed("virtual-provider", "2.4");
    versioned_provider.provides =
        "virtual-api (= 2.4)";
    PackageRecord versioned_consumer =
        installed("virtual-consumer", "1.0");
    versioned_consumer.depends =
        "virtual-api (>= 2.0)";

    const DebianResolution installed_versioned_provider =
        DebianDependencyResolver::resolve(
            {package("another-update", "3.0")},
            {versioned_consumer, versioned_provider},
            {}, "amd64", {});
    assert(installed_versioned_provider.complete());

    PackageRecord unversioned_provider =
        installed("old-virtual-provider", "9.0");
    unversioned_provider.provides = "versioned-api";
    PackageRecord strict_consumer =
        installed("strict-consumer", "1.0");
    strict_consumer.depends =
        "versioned-api (>= 2.0)";

    const DebianResolution unversioned_cannot_fake_version =
        DebianDependencyResolver::resolve(
            {package("third-update", "1.0")},
            {strict_consumer, unversioned_provider},
            {}, "amd64", {});
    assert(!unversioned_cannot_fake_version.complete());

    PackageRecord virtual_conflict_provider =
        installed("provider-package", "1.0");
    virtual_conflict_provider.provides =
        "conflicted-virtual (= 1.0)";
    DebianPackageVersion virtual_conflict =
        package("virtual-conflict-owner", "1.0");
    virtual_conflict.conflicts =
        "conflicted-virtual (>= 1.0)";
    const DebianResolution virtual_conflict_resolution =
        DebianDependencyResolver::resolve(
            {virtual_conflict},
            {virtual_conflict_provider},
            {},
            "amd64",
            {});
    assert(virtual_conflict_resolution.complete());
    assert(
        std::find(
            virtual_conflict_resolution.remove_installed.begin(),
            virtual_conflict_resolution.remove_installed.end(),
            "provider-package") !=
        virtual_conflict_resolution.remove_installed.end());

    DebianPackageVersion selected_virtual_provider =
        package("selected-provider", "1.0");
    selected_virtual_provider.provides =
        "selected-virtual (= 2.0)";
    DebianPackageVersion selected_virtual_blocker =
        package("selected-blocker", "1.0");
    selected_virtual_blocker.conflicts =
        "selected-virtual (>= 2.0)";
    const DebianResolution selected_virtual_conflict =
        DebianDependencyResolver::resolve(
            {selected_virtual_provider, selected_virtual_blocker},
            {},
            {},
            "amd64",
            {});
    assert(!selected_virtual_conflict.complete());

    DebianPackageVersion version_owner =
        package("version-owner", "1.0");
    version_owner.depends = "version-choice (>= 1.0)";
    DebianPackageVersion bad_newer =
        package("version-choice", "2.0");
    bad_newer.conflicts = "essential-guard";
    DebianPackageVersion good_older =
        package("version-choice", "1.5");
    PackageRecord essential_guard =
        installed("essential-guard", "1.0");
    essential_guard.essential = true;
    const DebianResolution version_backtrack =
        DebianDependencyResolver::resolve(
            {version_owner},
            {essential_guard},
            {bad_newer, good_older},
            "amd64",
            {});
    assert(version_backtrack.complete());
    const auto version_choice = std::find_if(
        version_backtrack.selected.begin(),
        version_backtrack.selected.end(),
        [](const DebianPackageVersion &candidate) {
            return candidate.package == "version-choice";
        });
    assert(version_choice != version_backtrack.selected.end());
    assert(version_choice->version == "1.5");

    PackageRecord reverse_blocker =
        installed("reverse-blocker", "1.0");
    reverse_blocker.conflicts =
        "reverse-target (>= 2.0)";
    const DebianResolution reverse_conflict =
        DebianDependencyResolver::resolve(
            {package("reverse-target", "2.0")},
            {reverse_blocker},
            {},
            "amd64",
            {});
    assert(!reverse_conflict.complete());

    DebianPackageVersion arch_owner =
        package("arch-owner", "1.0");
    arch_owner.conflicts = "arch-target:i386";
    const DebianResolution architecture_scoped_conflict =
        DebianDependencyResolver::resolve(
            {arch_owner},
            {installed("arch-target", "1.0", "amd64")},
            {},
            "amd64",
            {});
    assert(architecture_scoped_conflict.complete());

    PackageRecord protected_target =
        installed("protected-target", "1.0");
    protected_target.protected_package = true;
    DebianPackageVersion protected_conflicter =
        package("protected-conflicter", "1.0");
    protected_conflicter.conflicts = "protected-target";
    const DebianResolution protected_conflict =
        DebianDependencyResolver::resolve(
            {protected_conflicter},
            {protected_target},
            {},
            "amd64",
            {});
    assert(!protected_conflict.complete());
    assert(protected_conflict.remove_installed.empty());

    PackageRecord intrinsic_hold =
        installed("intrinsic-held", "1.0");
    intrinsic_hold.held = true;
    DebianPackageVersion held_owner =
        package("held-owner", "1.0");
    held_owner.depends = "intrinsic-held (>= 2.0)";
    const DebianResolution intrinsic_hold_resolution =
        DebianDependencyResolver::resolve(
            {held_owner},
            {intrinsic_hold},
            {package("intrinsic-held", "2.0")},
            "amd64",
            {});
    assert(!intrinsic_hold_resolution.complete());

    DebianPackageVersion repair_owner =
        package("repair-owner", "1.0");
    repair_owner.breaks = "repair-target (<< 2.0)";
    DebianPackageVersion bad_repair =
        package("repair-target", "3.0");
    bad_repair.conflicts = "repair-guard";
    DebianPackageVersion good_repair =
        package("repair-target", "2.1");
    PackageRecord repair_guard =
        installed("repair-guard", "1.0");
    repair_guard.essential = true;
    const DebianResolution repair_backtrack =
        DebianDependencyResolver::resolve(
            {repair_owner},
            {
                installed("repair-target", "1.0"),
                repair_guard
            },
            {bad_repair, good_repair},
            "amd64",
            {});
    assert(repair_backtrack.complete());
    const auto repair_choice = std::find_if(
        repair_backtrack.selected.begin(),
        repair_backtrack.selected.end(),
        [](const DebianPackageVersion &candidate) {
            return candidate.package == "repair-target";
        });
    assert(repair_choice != repair_backtrack.selected.end());
    assert(repair_choice->version == "2.1");

    DebianPackageVersion recommends_owner =
        package("recommends-owner", "1.0");
    recommends_owner.recommends = "recommended-helper";
    const DebianPackageVersion recommended_helper =
        package("recommended-helper", "1.0");

    const DebianResolution without_recommends =
        DebianDependencyResolver::resolve(
            {recommends_owner},
            {},
            {recommended_helper},
            "amd64",
            {},
            false);
    assert(without_recommends.complete());
    assert(!contains(
        without_recommends.selected,
        "recommended-helper"));

    const DebianResolution with_recommends =
        DebianDependencyResolver::resolve(
            {recommends_owner},
            {},
            {recommended_helper},
            "amd64",
            {},
            true);
    assert(with_recommends.complete());
    assert(contains(
        with_recommends.selected,
        "recommended-helper"));

    return 0;
}
