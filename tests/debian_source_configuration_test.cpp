// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_source_configuration.hpp"

#include <cassert>
#include <string>

using infiltrator::software::DebianSourceConfiguration;

int main()
{
    std::string error;

    const auto list = DebianSourceConfiguration::parse_list(
        "# disabled\n"
        "deb [arch=amd64,arm64 signed-by=/usr/share/keyrings/test.gpg] "
        "https://example.invalid/debian stable main contrib\n"
        "deb-src https://example.invalid/debian stable main\n",
        "sources.list",
        error);
    assert(error.empty());
    assert(list.size() == 1U);
    assert(list[0].uri == "https://example.invalid/debian");
    assert(list[0].suite == "stable");
    assert(list[0].components.size() == 2U);
    assert(list[0].architectures.size() == 2U);
    assert(list[0].keyrings.size() == 1U);
    assert(list[0].verify_signatures);

    const auto deb822 = DebianSourceConfiguration::parse_deb822(
        "Types: deb\n"
        "URIs: https://mirror.invalid/debian\n"
        "Suites: stable stable-updates\n"
        "Components: main\n"
        "Architectures: amd64\n"
        "Signed-By: /usr/share/keyrings/test.gpg\n"
        "\n"
        "Types: deb\n"
        "Enabled: no\n"
        "URIs: https://disabled.invalid/debian\n"
        "Suites: stable\n"
        "Components: main\n",
        "debian.sources",
        error);
    assert(error.empty());
    assert(deb822.size() == 2U);
    assert(deb822[0].suite == "stable");
    assert(deb822[1].suite == "stable-updates");
    assert(deb822[0].keyrings.size() == 1U);
    assert(deb822[0].architectures.size() == 1U);

    return 0;
}
