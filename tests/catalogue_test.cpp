// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue/repository_catalogue.hpp"

#include <cassert>
#include <string>

using infiltrator::software::RepositoryCatalogue;

int main()
{
    const std::string document = R"json(
[
  {
    "id": "calculator",
    "name": "Calculator",
    "category": "Productivity",
    "description": "Calculator description",
    "package": "infiltrator-calculator",
    "version": "1.2.3",
    "architecture": "amd64",
    "maintainer": "Shannon Smith",
    "package_description": "Native calculator",
    "asset": "calculator_1.2.3_amd64.deb",
    "download_size": 12345,
    "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "release_url": "https://example.invalid/release",
    "source_url": "https://example.invalid/source",
    "icon": "calculator",
    "icon_url": "catalogue/icons/calculator.svg",
    "icon_sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    "published_at": "2026-09-20T00:00:00Z"
  }
]
)json";

    std::string error;
    const auto records = RepositoryCatalogue::parse_document(
        document, "https://repo.example/", error);
    assert(error.empty());
    assert(records.size() == 1U);
    assert(records[0].id == "calculator");
    assert(records[0].package_name == "infiltrator-calculator");
    assert(records[0].name == "Calculator");
    assert(records[0].category == "Productivity");
    assert(records[0].available_version == "1.2.3");
    assert(records[0].download_size_bytes == 12345U);
    assert(records[0].icon_url ==
           "https://repo.example/catalogue/icons/calculator.svg");

    const std::string unsafe = R"json(
[
  {
    "id": "bad",
    "name": "Bad",
    "category": "System",
    "package": "bad",
    "version": "1.0.0",
    "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "icon_url": "../escape.svg",
    "icon_sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
  }
]
)json";

    error.clear();
    const auto rejected = RepositoryCatalogue::parse_document(
        unsafe, "https://repo.example/", error);
    assert(rejected.empty());
    assert(!error.empty());
    return 0;
}
