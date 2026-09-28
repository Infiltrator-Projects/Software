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

    const auto crlf = DebianSourceConfiguration::parse_deb822(
        "Types: deb\r\n"
        "URIs: https://one.invalid/debian\r\n"
        "Suites: stable\r\n"
        "Components: main\r\n"
        "\r\n"
        "Types: deb\r\n"
        "URIs: https://two.invalid/debian\r\n"
        "Suites: testing\r\n"
        "Components: main\r\n",
        "crlf.sources",
        error);
    assert(error.empty());
    assert(crlf.size() == 2U);
    assert(crlf[0].suite == "stable");
    assert(crlf[1].suite == "testing");

    const auto freshness = DebianSourceConfiguration::parse_deb822(
        "Types: deb\n"
        "URIs: https://fresh.invalid/debian\n"
        "Suites: stable\n"
        "Components: main\n"
        "Check-Valid-Until: no\n"
        "Check-Date: yes\n"
        "Valid-Until-Min: 60\n"
        "Valid-Until-Max: 3600\n"
        "Date-Max-Future: 30\n",
        "fresh.sources",
        error);
    assert(error.empty());
    assert(freshness.size() == 1U);
    assert(!freshness[0].check_valid_until);
    assert(freshness[0].check_date);
    assert(freshness[0].valid_until_min_seconds == 60U);
    assert(freshness[0].valid_until_max_seconds == 3600U);
    assert(freshness[0].date_max_future_seconds == 30U);

    const auto fingerprint = DebianSourceConfiguration::parse_deb822(
        "Types: deb\n"
        "URIs: https://fingerprint.invalid/debian\n"
        "Suites: stable\n"
        "Components: main\n"
        "Signed-By: /usr/share/keyrings/test.gpg "
        "0123456789ABCDEF0123456789ABCDEF01234567\n",
        "fingerprint.sources",
        error);
    assert(fingerprint.empty());
    assert(error.find("fingerprint") != std::string::npos);

    error.clear();
    const auto modifiers = DebianSourceConfiguration::parse_deb822(
        "Types: deb\n"
        "URIs: https://modifier.invalid/debian\n"
        "Suites: stable\n"
        "Components: main\n"
        "Architectures-Add: arm64\n",
        "modifier.sources",
        error);
    assert(modifiers.empty());
    assert(error.find("add/remove") != std::string::npos);

    return 0;
}
