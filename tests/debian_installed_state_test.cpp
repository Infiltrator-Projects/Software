// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_installed_state.hpp"

#include <cassert>
#include <cstdint>
#include <string>

int main()
{
    using namespace infiltrator::software;

    const std::string fixture =
        "package: alpha\n"
        "Status: install ok installed\n"
        "architecture: amd64\n"
        "Version: 1:2.3-4\n"
        "Installed-Size: 123\n"
        "Description: Alpha package\n"
        " continued description\n"
        "\n"
        "Package: old-config\n"
        "Status: deinstall ok config-files\n"
        "Architecture: amd64\n"
        "Version: 1.0\n"
        "Installed-Size: 9\n"
        "\n"
        "Package: held-package\n"
        "Status: hold ok installed\n"
        "Architecture: amd64\n"
        "Version: 5.0\n"
        "Installed-Size: invalid\n"
        "\n"
        "Package: libmulti\n"
        "Status: install ok installed\n"
        "Architecture: i386\n"
        "Multi-Arch: same\n"
        "Version: 2.0-1\n"
        "Installed-Size: 4\n"
        "\n"
        "Package: infiltrator-calendar\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Version: 1.0.45+nativepgo1\n"
        "Installed-Size: 1024\n";

    std::string error;
    const auto packages =
        DebianInstalledState::parse(fixture, error);

    assert(error.empty());
    assert(packages.size() == 4U);

    assert(packages[0].id == "alpha");
    assert(packages[0].architecture == "amd64");
    assert(packages[0].installed_version == "1:2.3-4");
    assert(packages[0].available_version == "1:2.3-4");
    assert(packages[0].installed_size_bytes == 123U * 1024U);
    assert(packages[0].source == "Debian");
    assert(packages[0].state == InstallState::installed);

    assert(packages[1].id == "held-package");
    assert(packages[1].installed_size_bytes == 0U);

    assert(packages[2].id == "infiltrator-calendar");
    assert(packages[2].architecture == "amd64");
    assert(packages[2].installed_version == "1.0.45+nativepgo1");
    assert(packages[2].state == InstallState::installed);

    assert(packages[3].id == "libmulti:i386");
    assert(packages[3].architecture == "i386");
    assert(packages[3].installed_size_bytes == 4U * 1024U);

    const auto whitespace_separator = DebianInstalledState::parse(
        "Package: one\nStatus: install ok installed\nVersion: 1\n   \n"
        "Package: two\nStatus: install ok installed\nVersion: 2\n",
        error);
    assert(error.empty());
    assert(whitespace_separator.size() == 2U);

    const auto duplicate = DebianInstalledState::parse(
        "Package: one\npackage: two\nStatus: install ok installed\nVersion: 1\n",
        error);
    assert(duplicate.empty());
    assert(!error.empty());

    return 0;
}
