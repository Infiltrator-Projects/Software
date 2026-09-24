// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_package_index.hpp"

#include <cassert>
#include <cstdint>
#include <string>

int main()
{
    using namespace infiltrator::software;

    const std::string fixture =
        "pAcKaGe: alpha\n"
        "Version: 1:2.3-4\n"
        "ARCHITECTURE: amd64\n"
        "Priority: optional\n"
        "Source: alpha-source (1:2.3-4)\n"
        "Phased-Update-Percentage: 40\n"
        "Essential: yes\n"
        "Multi-Arch: same\n"
        "Depends: libc6 (>= 2.38), libssl3 | libssl3t64\n"
        "Pre-Depends: init-system-helpers (>= 1.54~)\n"
        "Recommends: alpha-data\n"
        "Provides: virtual-alpha (= 1:2.3-4)\n"
        "Conflicts: old-alpha\n"
        "Breaks: alpha-common (<< 1:2.3-4)\n"
        "Replaces: old-alpha\n"
        "Filename: pool/main/a/alpha/alpha_2.3-4_amd64.deb\n"
        "Size: 2048\n"
        "Installed-Size: 12\n"
        "SHA256: 0123456789abcdef\n"
        "Description: Alpha package\n"
        " continued details\n"
        "\n"
        "Package: data-only\n"
        "Version: 5.0-1\n"
        "Architecture: all\n"
        "Filename: pool/main/d/data-only.deb\n"
        "Size: not-a-number\n"
        "Installed-Size: 3\n"
        "\n"
        "Package: malformed\n"
        "Version: 1.0\n"
        "\n";

    std::string error;
    const auto packages =
        DebianPackageIndex::parse(
            fixture, "example stable/main", error);

    assert(error.empty());
    assert(packages.size() == 2U);

    const auto &alpha = packages[0];
    assert(alpha.package == "alpha");
    assert(alpha.version == "1:2.3-4");
    assert(alpha.architecture == "amd64");
    assert(alpha.source == "example stable/main");
    assert(alpha.priority == "optional");
    assert(alpha.source_package == "alpha-source");
    assert(alpha.source_version == "1:2.3-4");
    assert(alpha.phased_update_percentage == 40);
    assert(alpha.essential);
    assert(alpha.multi_arch == "same");
    assert(alpha.depends ==
           "libc6 (>= 2.38), libssl3 | libssl3t64");
    assert(alpha.pre_depends == "init-system-helpers (>= 1.54~)");
    assert(alpha.recommends == "alpha-data");
    assert(alpha.provides == "virtual-alpha (= 1:2.3-4)");
    assert(alpha.conflicts == "old-alpha");
    assert(alpha.breaks == "alpha-common (<< 1:2.3-4)");
    assert(alpha.replaces == "old-alpha");
    assert(alpha.filename ==
           "pool/main/a/alpha/alpha_2.3-4_amd64.deb");
    assert(alpha.size_bytes == 2048U);
    assert(alpha.installed_size_bytes == 12U * 1024U);
    assert(alpha.sha256 == "0123456789abcdef");
    assert(alpha.description == "Alpha package\ncontinued details");

    const auto &data = packages[1];
    assert(data.package == "data-only");
    assert(data.architecture == "all");
    assert(data.size_bytes == 0U);
    assert(data.installed_size_bytes == 3U * 1024U);
    assert(!data.essential);

    const auto duplicate = DebianPackageIndex::parse(
        "Package: one\nPACKAGE: two\nVersion: 1\nArchitecture: amd64\n",
        "test", error);
    assert(duplicate.empty());
    assert(!error.empty());

    const auto malformed = DebianPackageIndex::parse(
        "Package: one\nthis is not a field\nVersion: 1\nArchitecture: amd64\n",
        "test", error);
    assert(malformed.empty());
    assert(!error.empty());

    return 0;
}
