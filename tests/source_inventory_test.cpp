// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/source_inventory.hpp"
#include "sources/source_mutation.hpp"

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
        assert(records[0].entry_index == 1U);
        assert(!records[1].enabled);
        assert(records[1].location == "https://disabled.invalid");
        assert(records[1].entry_index == 2U);

        std::string updated;
        std::string error;
        assert(infiltrator::software::set_apt_list_entry_enabled(
            list, records[0].entry_index, false, updated, error));
        assert(error.empty());
        const auto disabled =
            SourceInventory::parse_apt_list(
                updated, "/etc/apt/sources.list.d/example.list");
        assert(disabled.size() == 2U);
        assert(!disabled[0].enabled);

        assert(infiltrator::software::set_apt_list_entry_enabled(
            updated, disabled[1].entry_index, true, updated, error));
        assert(error.empty());
        const auto enabled =
            SourceInventory::parse_apt_list(
                updated, "/etc/apt/sources.list.d/example.list");
        assert(enabled.size() == 2U);
        assert(enabled[1].enabled);
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
        assert(records[0].entry_index == 1U);
        assert(records[0].detail.find("main contrib") != std::string::npos);
        assert(!records[1].enabled);
        assert(records[1].entry_index == 2U);

        std::string updated;
        std::string error;
        assert(infiltrator::software::set_apt_deb822_entry_enabled(
            deb822, records[1].entry_index, true, updated, error));
        assert(error.empty());
        const auto enabled =
            SourceInventory::parse_apt_deb822(
                updated, "/etc/apt/sources.list.d/example.sources");
        assert(enabled.size() == 2U);
        assert(enabled[1].enabled);
    }

    {
        const std::string deb822 =
            "Types: deb\n"
            "URIs: https://no-enabled-field.invalid\n"
            "Suites: stable\n"
            "Components: main\n\n"
            "Types: deb\n"
            "URIs: https://second.invalid\n"
            "Suites: stable\n"
            "Components: main\n";

        const auto records =
            SourceInventory::parse_apt_deb822(
                deb822, "/etc/apt/sources.list.d/no-enabled.sources");
        assert(records.size() == 2U);
        assert(records[0].entry_index == 1U);
        assert(records[1].entry_index == 2U);

        std::string updated;
        std::string error;
        assert(infiltrator::software::set_apt_deb822_entry_enabled(
            deb822, records[0].entry_index, false, updated, error));
        assert(error.empty());
        assert(updated.find("Enabled: no\n\nTypes: deb") !=
               std::string::npos);

        const auto disabled =
            SourceInventory::parse_apt_deb822(
                updated, "/etc/apt/sources.list.d/no-enabled.sources");
        assert(disabled.size() == 2U);
        assert(!disabled[0].enabled);
        assert(disabled[1].enabled);
        assert(disabled[1].entry_index == 2U);
    }

    {
        std::string deb822 =
            "Types: deb\r\n"
            "URIs: https://crlf-one.invalid\r\n"
            "Suites: stable\r\n"
            "Components: main\r\n"
            "Enabled: yes\r\n\r\n"
            "Types: deb\r\n"
            "URIs: https://crlf-two.invalid\r\n"
            "Suites: testing\r\n"
            "Components: main\r\n"
            "Enabled: yes\r\n";

        auto records =
            SourceInventory::parse_apt_deb822(
                deb822,
                "/etc/apt/sources.list.d/crlf.sources");
        assert(records.size() == 2U);
        assert(records[0].entry_index == 1U);
        assert(records[1].entry_index == 2U);

        std::string error;
        assert(infiltrator::software::set_apt_deb822_entry_enabled(
            deb822, records[0].entry_index, false, deb822, error));
        assert(error.empty());
        assert(
            deb822.find(
                "Enabled: no\r\n\r\nTypes: deb") !=
            std::string::npos);

        records =
            SourceInventory::parse_apt_deb822(
                deb822,
                "/etc/apt/sources.list.d/crlf.sources");
        assert(records.size() == 2U);
        assert(!records[0].enabled);
        assert(records[1].enabled);
    }

    return 0;
}
