// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/apt_source_format.hpp"
#include "sources/source_inventory.hpp"
#include "sources/source_mutation.hpp"

#include <cassert>
#include <string>

int main()
{
    using namespace infiltrator::software;

    const std::string continued =
        "# repository identity may use deb822 continuations\n"
        "Types: deb\n"
        "URIs:\n"
        " https://continued.example.invalid/debian\n"
        "Suites:\n"
        " stable\n"
        " updates\n"
        "Components: main contrib\n"
        "Enabled: yes\n"
        "   \n"
        "Types: deb\r\n"
        "URIs: https://second.example.invalid/debian\r\n"
        "Suites: testing\r\n"
        "Components: main\r\n"
        "Enabled: no\r\n";

    const auto stanzas = parse_apt_deb822_stanzas(continued);
    assert(stanzas.size() == 2U);
    assert(stanzas[0].valid);
    assert(stanzas[0].number == 1U);
    assert(stanzas[0].fields.at("uris") ==
           "https://continued.example.invalid/debian");
    assert(stanzas[0].fields.at("suites") == "stable updates");
    assert(stanzas[1].valid);
    assert(stanzas[1].number == 2U);

    auto records = SourceInventory::parse_apt_deb822(
        continued,
        "/etc/apt/sources.list.d/continued.sources");
    assert(records.size() == 2U);
    assert(records[0].location ==
           "https://continued.example.invalid/debian");
    assert(records[0].apt_suites == "stable updates");
    assert(records[0].entry_index == 1U);
    assert(records[0].enabled);
    assert(records[1].entry_index == 2U);
    assert(!records[1].enabled);

    std::string updated;
    std::string error;
    assert(set_apt_deb822_entry_enabled(
        continued,
        records[0].entry_index,
        false,
        records[0].location,
        records[0].apt_suites,
        updated,
        error));
    assert(error.empty());

    records = SourceInventory::parse_apt_deb822(
        updated,
        "/etc/apt/sources.list.d/continued.sources");
    assert(records.size() == 2U);
    assert(!records[0].enabled);
    assert(!records[1].enabled);

    const std::string duplicate =
        "Types: deb\n"
        "URIs: https://one.invalid\n"
        "URIs: https://two.invalid\n"
        "Suites: stable\n";
    const auto invalid = parse_apt_deb822_stanzas(duplicate);
    assert(invalid.size() == 1U);
    assert(!invalid[0].valid);
    assert(SourceInventory::parse_apt_deb822(
               duplicate,
               "/etc/apt/sources.list.d/invalid.sources")
               .empty());

    return 0;
}
