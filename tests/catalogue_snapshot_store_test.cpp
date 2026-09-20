// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue/catalogue_snapshot_store.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

std::filesystem::path make_temp_directory()
{
    std::string pattern =
        (std::filesystem::temp_directory_path() /
         "infiltrator-software-catalogue-store-XXXXXX").string();
    std::vector<char> writable(
        pattern.begin(),
        pattern.end());
    writable.push_back('\0');
    char *created =
        mkdtemp(writable.data());
    assert(created != nullptr);
    return std::filesystem::path(created);
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    const std::filesystem::path directory =
        make_temp_directory();
    const std::filesystem::path path =
        directory / "discover.json";

    CatalogueSnapshot snapshot;
    snapshot.source =
        "Infiltrator + system";

    PackageRecord record;
    record.id = "calculator";
    record.name = "Calculator";
    record.package_name =
        "infiltrator-calculator";
    record.publisher = "Shannon Smith";
    record.category = "Productivity";
    record.architecture = "amd64";
    record.installed_version = "1.0";
    record.available_version = "1.1";
    record.summary = "Native calculator";
    record.description =
        "Calculator description";
    record.icon_name = "calculator";
    record.icon_url =
        "https://example.invalid/icon.svg";
    record.icon_sha256 =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    record.cached_icon_path =
        "/tmp/calculator.svg";
    record.source =
        "Infiltrator Repository";
    record.source_url =
        "https://example.invalid/source";
    record.release_url =
        "https://example.invalid/release";
    record.asset =
        "calculator_1.1_amd64.deb";
    record.package_sha256 =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    record.published_at =
        "2026-09-20T00:00:00Z";
    record.channel = Channel::beta;
    record.kind = PackageKind::application;
    record.state = InstallState::upgradable;
    record.installed_size_bytes = 1000U;
    record.download_size_bytes = 500U;
    snapshot.records.push_back(record);

    CatalogueSnapshotStore store(
        path.string());
    std::string error;
    assert(store.save(snapshot, error));
    assert(error.empty());

    CatalogueSnapshot loaded;
    assert(store.load(loaded, error));
    assert(error.empty());
    assert(loaded.from_cache);
    assert(loaded.source == snapshot.source);
    assert(loaded.records.size() == 1U);
    assert(loaded.records[0].id == record.id);
    assert(
        loaded.records[0].available_version ==
        "1.1");
    assert(
        loaded.records[0].channel ==
        Channel::beta);
    assert(
        loaded.records[0].state ==
        InstallState::upgradable);
    assert(
        loaded.records[0].download_size_bytes ==
        500U);
    assert(
        loaded.records[0].cached_icon_path ==
        "/tmp/calculator.svg");

    {
        std::ofstream corrupt(
            path,
            std::ios::trunc);
        corrupt << "{ definitely not json";
    }

    CatalogueSnapshot rejected;
    assert(!store.load(rejected, error));
    assert(!error.empty());

    std::filesystem::remove_all(directory);
    return 0;
}
