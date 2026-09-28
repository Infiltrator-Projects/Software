// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/source_inventory.hpp"
#include "sources/source_mutation.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

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
            list, records[0].entry_index, false,
            records[0].location, records[0].apt_suites,
            updated, error));
        assert(error.empty());
        const auto disabled =
            SourceInventory::parse_apt_list(
                updated, "/etc/apt/sources.list.d/example.list");
        assert(disabled.size() == 2U);
        assert(!disabled[0].enabled);

        assert(infiltrator::software::set_apt_list_entry_enabled(
            updated, disabled[1].entry_index, true,
            disabled[1].location, disabled[1].apt_suites,
            updated, error));
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
            deb822, records[1].entry_index, true,
            records[1].location, records[1].apt_suites,
            updated, error));
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
            deb822, records[0].entry_index, false,
            records[0].location, records[0].apt_suites,
            updated, error));
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
            deb822, records[0].entry_index, false,
            records[0].location, records[0].apt_suites,
            deb822, error));
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

    {
        const std::string source_only =
            "Types: deb-src\n"
            "URIs: https://source-only.invalid/debian\n"
            "Suites: stable\n"
            "Components: main\n"
            "Enabled: yes\n";
        const auto records =
            SourceInventory::parse_apt_deb822(
                source_only,
                "/etc/apt/sources.list.d/source-only.sources");
        assert(records.size() == 1U);
        assert(records[0].enabled);
        std::string updated;
        std::string error;
        assert(infiltrator::software::set_apt_deb822_entry_enabled(
            source_only,
            records[0].entry_index,
            false,
            records[0].location,
            records[0].apt_suites,
            updated,
            error));
        assert(error.empty());
        const auto disabled =
            SourceInventory::parse_apt_deb822(
                updated,
                "/etc/apt/sources.list.d/source-only.sources");
        assert(disabled.size() == 1U);
        assert(!disabled[0].enabled);
    }

    {
        const std::string original =
            "deb https://first.invalid stable main\n"
            "deb https://second.invalid testing main\n";
        const auto records =
            SourceInventory::parse_apt_list(
                original,
                "/etc/apt/sources.list.d/stale.list");
        assert(records.size() == 2U);

        const std::string shifted =
            "deb https://inserted.invalid unstable main\n" +
            original;
        std::string updated;
        std::string error;
        assert(!infiltrator::software::set_apt_list_entry_enabled(
            shifted,
            records[1].entry_index,
            false,
            records[1].location,
            records[1].apt_suites,
            updated,
            error));
        assert(error.find("changed since it was reviewed") !=
               std::string::npos);
    }

    {
        const std::string original =
            "Types: deb\n"
            "URIs: https://first.invalid/debian\n"
            "Suites: stable\n"
            "Components: main\n\n"
            "Types: deb\n"
            "URIs: https://second.invalid/debian\n"
            "Suites: testing\n"
            "Components: main\n";
        const auto records =
            SourceInventory::parse_apt_deb822(
                original,
                "/etc/apt/sources.list.d/stale.sources");
        assert(records.size() == 2U);

        const std::string shifted =
            "Types: deb\n"
            "URIs: https://inserted.invalid/debian\n"
            "Suites: unstable\n"
            "Components: main\n\n" +
            original;
        std::string updated;
        std::string error;
        assert(!infiltrator::software::set_apt_deb822_entry_enabled(
            shifted,
            records[1].entry_index,
            false,
            records[1].location,
            records[1].apt_suites,
            updated,
            error));
        assert(error.find("changed since it was reviewed") !=
               std::string::npos);
    }

    {
        namespace fs = std::filesystem;
        const fs::path root =
            fs::temp_directory_path() /
            ("software-source-xdg-test-" +
             std::to_string(
                 static_cast<unsigned long long>(
                     getpid())));
        const fs::path data =
            root / "xdg-data";
        const fs::path config =
            data / "flatpak/repo/config";
        fs::create_directories(
            config.parent_path());
        {
            std::ofstream output(config);
            assert(output);
            output
                << "[remote \"user-test\"]\n"
                << "url=https://flatpak.example.invalid/repo\n"
                << "xa.title=User Test Remote\n"
                << "xa.disable=false\n";
        }

        const char *old =
            std::getenv("XDG_DATA_HOME");
        const std::string old_value =
            old == nullptr ? std::string{} : std::string(old);
        (void)setenv(
            "XDG_DATA_HOME",
            data.c_str(),
            1);

        SourceInventory inventory;
        std::string error;
        const auto records =
            inventory.list(error);
        assert(error.empty());
        bool found = false;
        for (const auto &record : records) {
            if (record.kind ==
                    infiltrator::software::SourceKind::flatpak &&
                record.scope == "User" &&
                record.name == "user-test") {
                found = true;
                assert(
                    record.location ==
                    "https://flatpak.example.invalid/repo");
            }
        }
        assert(found);

        if (old != nullptr) {
            (void)setenv(
                "XDG_DATA_HOME",
                old_value.c_str(),
                1);
        } else {
            (void)unsetenv(
                "XDG_DATA_HOME");
        }
        fs::remove_all(root);
    }

    return 0;
}
