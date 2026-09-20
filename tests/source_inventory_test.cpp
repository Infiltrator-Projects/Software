// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/source_inventory.hpp"

#include <cassert>
#include <string>

using infiltrator::software::SourceInventory;

int main()
{
    {
        const std::string list =
            "deb [arch=amd64 signed-by=/usr/share/keyrings/example.gpg] "
            "https://example.invalid stable main\n"
            "# deb https://disabled.invalid testing main contrib\n";

        const auto records =
            SourceInventory::parse_apt_list(
                list, "/etc/apt/sources.list.d/example.list");
        assert(records.size() == 2U);
        assert(records[0].enabled);
        assert(records[0].location == "https://example.invalid");
        assert(!records[1].enabled);
        assert(records[1].location == "https://disabled.invalid");
    }

    {
        const std::string deb822 =
            "Types: deb\n"
            "URIs: https://packages.example.invalid\n"
            "Suites: stable updates\n"
            "Components: main contrib\n"
            "Enabled: yes\n\n"
            "Types: deb\n"
            "URIs: https://disabled.example.invalid\n"
            "Suites: testing\n"
            "Components: main\n"
            "Enabled: no\n";

        const auto records =
            SourceInventory::parse_apt_deb822(
                deb822, "/etc/apt/sources.list.d/example.sources");
        assert(records.size() == 2U);
        assert(records[0].enabled);
        assert(records[0].detail.find("main contrib") != std::string::npos);
        assert(!records[1].enabled);
    }

    return 0;
}
