// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_candidate.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace {

infiltrator::software::PackageRecord installed(
    const std::string &name,
    const std::string &version,
    const std::string &architecture = "amd64")
{
    using namespace infiltrator::software;
    PackageRecord package;
    package.id = name;
    package.name = name;
    package.package_name = name;
    package.architecture = architecture;
    package.installed_version = version;
    package.available_version = version;
    package.state = InstallState::installed;
    return package;
}

infiltrator::software::DebianPackageVersion available(
    const std::string &name,
    const std::string &version,
    const std::string &source,
    const std::string &architecture = "amd64")
{
    using namespace infiltrator::software;
    DebianPackageVersion package;
    package.package = name;
    package.version = version;
    package.source = source;
    package.architecture = architecture;
    package.filename =
        "pool/" + name + "_" + version + "_" + architecture + ".deb";
    return package;
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    std::vector<PackageRecord> current{
        installed("alpha", "1.0"),
        installed("beta", "5.0"),
        installed("gamma", "3.0"),
        installed("held", "1.0"),
        installed("multi:i386", "2.0", "i386"),
        installed("low-priority", "1.0"),
        installed("same", "2.0"),
        installed("infiltrator-calendar", "1.0.45+nativepgo1"),
        installed("native-current", "1.0.46+nativepgo1")
    };

    std::vector<DebianPackageVersion> repository{
        available("alpha", "1.1", "stable"),
        available("alpha", "1.2", "testing"),
        available("beta", "4.0", "stable"),
        available("gamma", "2.0", "forced"),
        available("held", "9.0", "stable"),
        available("multi", "2.1", "stable", "amd64"),
        available("multi", "2.2", "stable", "i386"),
        available("multi", "2.3", "foreign", "arm64"),
        available("low-priority", "2.0", "low"),
        available("same", "2.0", "stable", "all"),
        available("same", "2.0", "testing", "amd64"),
        available("infiltrator-calendar", "1.0.46", "stable"),
        available("native-current", "1.0.46", "stable")
    };

    DebianCandidatePolicy policy;
    policy.source_priorities["stable"] = 500;
    policy.source_priorities["testing"] = 400;
    policy.source_priorities["forced"] = 1001;
    policy.source_priorities["foreign"] = 900;
    policy.source_priorities["low"] = 50;
    policy.held_packages.insert("held");

    const auto selected =
        DebianCandidateSelector::select(current, repository, policy);

    assert(selected.size() == current.size());

    const auto find = [&](const std::string &id)
        -> const DebianCandidateSelection & {
        for (const auto &selection : selected) {
            if (selection.installed.id == id) {
                return selection;
            }
        }
        assert(false);
        return selected.front();
    };

    const auto &alpha = find("alpha");
    assert(alpha.candidate.has_value());
    assert(alpha.candidate->version == "1.1");
    assert(alpha.candidate->source == "stable");
    assert(alpha.candidate_priority == 500);
    assert(alpha.upgrade_available);
    assert(!alpha.downgrade_selected);

    const auto &beta = find("beta");
    assert(!beta.candidate.has_value());
    assert(!beta.upgrade_available);
    assert(!beta.downgrade_selected);

    const auto &gamma = find("gamma");
    assert(gamma.candidate.has_value());
    assert(gamma.candidate->version == "2.0");
    assert(gamma.candidate_priority == 1001);
    assert(!gamma.upgrade_available);
    assert(gamma.downgrade_selected);

    const auto &held_selection = find("held");
    assert(held_selection.held);
    assert(!held_selection.candidate.has_value());

    const auto &multi = find("multi:i386");
    assert(multi.candidate.has_value());
    assert(multi.candidate->version == "2.2");
    assert(multi.candidate->architecture == "i386");
    assert(multi.upgrade_available);

    const auto &low = find("low-priority");
    assert(!low.candidate.has_value());
    assert(!low.upgrade_available);

    const auto &same = find("same");
    assert(same.candidate.has_value());
    assert(same.candidate->version == "2.0");
    assert(same.candidate->source == "stable");
    assert(!same.upgrade_available);
    assert(!same.downgrade_selected);

    const auto &native_upgrade = find("infiltrator-calendar");
    assert(native_upgrade.candidate.has_value());
    assert(native_upgrade.candidate->version == "1.0.46");
    assert(native_upgrade.upgrade_available);
    assert(!native_upgrade.downgrade_selected);

    const auto &native_current = find("native-current");
    assert(!native_current.candidate.has_value());
    assert(!native_current.upgrade_available);
    assert(!native_current.downgrade_selected);

    return 0;
}
