// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue/repository_catalogue.hpp"
#include "catalogue/system_catalogue.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using infiltrator::software::InstallState;
using infiltrator::software::RepositoryCatalogue;
using infiltrator::software::SystemCatalogue;

namespace {

void write_script(
    const std::filesystem::path &path,
    const std::string &body)
{
    std::ofstream output(path);
    assert(output);
    output << "#!/bin/sh\n" << body;
    output.close();
    assert(output);
    assert(chmod(path.c_str(), 0755) == 0);
}

} // namespace

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

    namespace fs = std::filesystem;
    const fs::path root =
        fs::temp_directory_path() /
        ("software-system-catalogue-test-" +
         std::to_string(
             static_cast<unsigned long long>(
                 getpid())));
    const fs::path bin = root / "bin";
    const fs::path data = root / "data";
    fs::create_directories(bin);
    fs::create_directories(
        data / "flatpak/app/org.example.App");

    write_script(
        bin / "flatpak",
        R"(if [ "$1" = remote-ls ]; then
  if [ "$2" = --system ]; then
    printf 'org.example.App\tExample App\tSystem copy\tstable\tflathub\n'
  else
    printf 'org.example.App\tExample App\tUser copy\tstable\tflathub\n'
    printf 'org.example.UserOnly\tUser Only\tUser application\tstable\tflathub\n'
  fi
  exit 0
fi
exit 1
)");

    const char *old_path = std::getenv("PATH");
    const char *old_data = std::getenv("XDG_DATA_HOME");
    const std::string path =
        bin.string() + ":" +
        (old_path == nullptr ? "" : old_path);
    (void)setenv("PATH", path.c_str(), 1);
    (void)setenv("XDG_DATA_HOME", data.c_str(), 1);

    SystemCatalogue system_catalogue;
    error.clear();
    const auto system_snapshot =
        system_catalogue.refresh(error);

    const auto find_record =
        [&](const std::string &id)
            -> const infiltrator::software::PackageRecord * {
            for (const auto &record :
                 system_snapshot.records) {
                if (record.id == id) {
                    return &record;
                }
            }
            return nullptr;
        };

    const auto *system_flatpak =
        find_record("flatpak:system:org.example.App");
    const auto *user_flatpak =
        find_record("flatpak:user:org.example.App");
    const auto *user_only =
        find_record("flatpak:user:org.example.UserOnly");
    assert(system_flatpak != nullptr);
    assert(user_flatpak != nullptr);
    assert(user_only != nullptr);
    assert(
        system_flatpak->state ==
        InstallState::not_installed);
    assert(
        user_flatpak->state ==
        InstallState::installed);
    assert(user_flatpak->repository_origin == "flathub");

    error.clear();
    const auto installed_flatpaks =
        system_catalogue.installed_flatpaks(error);
    assert(error.empty());
    bool found_user_install = false;
    for (const auto &record :
         installed_flatpaks) {
        if (record.id ==
            "flatpak:user:org.example.App") {
            found_user_install = true;
        }
    }
    assert(found_user_install);

    if (old_data != nullptr) {
        (void)setenv(
            "XDG_DATA_HOME",
            old_data,
            1);
    } else {
        (void)unsetenv("XDG_DATA_HOME");
    }
    if (old_path != nullptr) {
        (void)setenv("PATH", old_path, 1);
    }
    fs::remove_all(root);
    return 0;
}
