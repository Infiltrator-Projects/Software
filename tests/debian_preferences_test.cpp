// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_preferences.hpp"

#include <cassert>
#include <string>

namespace {

infiltrator::software::DebianPackageVersion package(
    const std::string &name,
    const std::string &version,
    const std::string &origin,
    const std::string &archive,
    const std::string &codename,
    const std::string &component,
    const std::string &site,
    const int default_priority = 500)
{
    using namespace infiltrator::software;
    DebianPackageVersion result;
    result.package = name;
    result.version = version;
    result.architecture = "amd64";
    result.release_origin = origin;
    result.release_archive = archive;
    result.release_codename = codename;
    result.component = component;
    result.site = site;
    result.pin_priority = default_priority;
    return result;
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    const std::string preferences =
R"(Package: *
Pin: release o=linuxmint,c=upstream
Pin-Priority: 700

Package: *
Pin: origin packages.example.invalid
Pin-Priority: 650

Package: snapd
Pin: release a=*
Pin-Priority: -10

Package: firefox
Pin: version 150.*
Pin-Priority: 1000
)";

    std::string error;
    const DebianAptPreferences host_policy =
        DebianAptPreferences::parse(
            preferences,
            "fixture.pref",
            error);
    assert(error.empty());
    assert(!host_policy.empty());

    DebianPolicyStack policy;
    policy.add(host_policy);

    const auto mint = package(
        "base-files",
        "13ubuntu10mint22.3.0",
        "linuxmint",
        "zena",
        "zena",
        "upstream",
        "packages.linuxmint.com");
    assert(policy.evaluate(mint).priority == 700);

    const auto ubuntu = package(
        "base-files",
        "13ubuntu10.5",
        "Ubuntu",
        "noble-updates",
        "noble",
        "main",
        "archive.ubuntu.com");
    assert(policy.evaluate(ubuntu).priority == 500);

    const auto third_party = package(
        "example",
        "2.0",
        "Example",
        "stable",
        "stable",
        "main",
        "packages.example.invalid");
    assert(policy.evaluate(third_party).priority == 650);

    const auto snap = package(
        "snapd",
        "2.0",
        "Ubuntu",
        "noble",
        "noble",
        "main",
        "archive.ubuntu.com");
    assert(policy.evaluate(snap).priority == -10);

    const auto firefox = package(
        "firefox",
        "150.0~build1",
        "Mozilla",
        "mozilla",
        "mozilla",
        "main",
        "packages.mozilla.org");
    assert(policy.evaluate(firefox).priority == 1000);

    const auto backports = package(
        "backported",
        "1.0",
        "Ubuntu",
        "noble-backports",
        "noble",
        "main",
        "archive.ubuntu.com",
        100);
    assert(policy.evaluate(backports).priority == 100);

    return 0;
}
