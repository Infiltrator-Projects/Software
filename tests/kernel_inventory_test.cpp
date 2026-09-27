// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/kernel_inventory.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace {

using namespace infiltrator::software;

PackageRecord installed(const std::string &name, const std::string &version)
{
    PackageRecord package;
    package.id = name;
    package.name = name;
    package.package_name = name;
    package.architecture = "amd64";
    package.installed_version = version;
    package.available_version = version;
    package.state = InstallState::installed;
    return package;
}

DebianPackageVersion available(
    const std::string &name,
    const std::string &version,
    const std::string &supported = "5y")
{
    DebianPackageVersion package;
    package.package = name;
    package.version = version;
    package.architecture = "amd64";
    package.source = "ubuntu noble/main";
    package.source_package = "linux";
    package.release_origin = "Ubuntu";
    package.release_archive = "noble-updates";
    package.supported = supported;
    package.filename = "pool/" + name + "_" + version + "_amd64.deb";
    package.sha256 = "deadbeef";
    return package;
}

const KernelRecord *find(
    const std::vector<KernelRecord> &records,
    const std::string &version)
{
    const auto it = std::find_if(
        records.begin(), records.end(),
        [&](const KernelRecord &record) {
            return record.version == version &&
                   record.kernel_type == "-generic";
        });
    return it == records.end() ? nullptr : &*it;
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    const std::vector<PackageRecord> installed_packages{
        installed("linux-image-7.0.0-34-generic", "7.0.0-34.34"),
        installed("linux-modules-7.0.0-34-generic", "7.0.0-34.34"),
        installed("linux-headers-7.0.0-34", "7.0.0-34.34"),
        installed("linux-headers-7.0.0-34-generic", "7.0.0-34.34"),
        installed("linux-image-7.0.0-31-generic", "7.0.0-31.31"),
        installed("linux-modules-7.0.0-31-generic", "7.0.0-31.31"),
        installed("linux-headers-7.0.0-31", "7.0.0-31.31"),
        installed("linux-headers-7.0.0-31-generic", "7.0.0-31.31"),
        installed("linux-image-7.0.0-30-generic", "7.0.0-30.30"),
        installed("linux-modules-7.0.0-30-generic", "7.0.0-30.30"),
        installed("linux-headers-7.0.0-30", "7.0.0-30.30"),
        installed("linux-headers-7.0.0-30-generic", "7.0.0-30.30")
    };

    std::vector<DebianPackageVersion> available_packages;
    for (const char *release :
         {"7.0.0-34", "7.0.0-31", "7.0.0-30", "7.0.0-35"}) {
        const std::string base(release);
        available_packages.push_back(
            available(
                "linux-image-" + base + "-generic",
                base + ".1"));
        available_packages.push_back(
            available(
                "linux-modules-" + base + "-generic",
                base + ".1"));
        available_packages.push_back(
            available(
                "linux-headers-" + base,
                base + ".1"));
        available_packages.push_back(
            available(
                "linux-headers-" + base + "-generic",
                base + ".1"));
    }

    const std::vector<KernelReleaseWindow> windows{
        {"noble", 2024, 4, 2029, 5}
    };

    const auto kernels =
        KernelInventory::build(
            installed_packages,
            available_packages,
            "-generic",
            "7.0.0-34-generic",
            2026,
            9,
            windows);

    const KernelRecord *active = find(kernels, "7.0.0-34");
    const KernelRecord *fallback = find(kernels, "7.0.0-31");
    const KernelRecord *old = find(kernels, "7.0.0-30");
    const KernelRecord *future = find(kernels, "7.0.0-35");

    assert(active != nullptr);
    assert(active->active);
    assert(active->installed);
    assert(!active->safe_to_remove);
    assert(active->supported);
    assert(active->support_status.find("Supported until") == 0U);

    assert(fallback != nullptr);
    assert(fallback->installed);
    assert(fallback->superseded);
    assert(!fallback->safe_to_remove);

    assert(old != nullptr);
    assert(old->installed);
    assert(old->superseded);
    assert(old->safe_to_remove);
    assert(!old->remove_package_ids.empty());

    assert(future != nullptr);
    assert(!future->installed);
    assert(future->installable);
    assert(!future->install_package_ids.empty());

    assert(KernelInventory::supported_kernel_type("-generic"));
    assert(KernelInventory::supported_kernel_type("-lowlatency"));
    assert(!KernelInventory::supported_kernel_type("-made-up"));

    return 0;
}
