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

    DebianPackageVersion conflict = package("new-tool", "2.0");
    conflict.conflicts = "old-tool (<< 2.0)";
    const DebianResolution conflicting =
        DebianDependencyResolver::resolve(
            {conflict},
            {installed("old-tool", "1.5")},
            {},
            "amd64",
            {});
    assert(!conflicting.complete());
    assert(conflicting.problems.size() == 1U);
    assert(conflicting.problems[0].package == "new-tool");

    return 0;
}
