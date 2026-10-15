// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_apt_configuration.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>

namespace {

void write_text(
    const std::filesystem::path &path,
    const std::string &content)
{
    std::ofstream output(path);
    assert(output);
    output << content;
    output.close();
    assert(output);
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    using namespace infiltrator::software;

    std::string error;
    const auto parsed = DebianAptConfiguration::parse(
        R"(
            // Names are case-insensitive and scopes compose with :: names.
            APT {
                Default-Release "stable";
                Get { Always-Include-Phased-Updates "true"; };
            };
            #clear APT::Default-Release;
            apt::Default-Release "testing";
        )",
        "fixture",
        error);
    assert(parsed.has_value());
    assert(error.empty());
    assert(parsed->value("APT::Default-Release") ==
           std::optional<std::string>("testing"));
    assert(parsed->value(
               "apt::get::always-include-phased-updates") ==
           std::optional<std::string>("true"));

    const fs::path root =
        fs::temp_directory_path() /
        ("software-apt-config-test-" +
         std::to_string(
             static_cast<unsigned long long>(::getpid())));
    fs::create_directories(root / "parts");

    write_text(
        root / "bootstrap.conf",
        "Dir::Etc \"" + root.string() + "\";\n"
        "Dir::Etc::Parts \"parts\";\n"
        "Dir::Etc::Main \"main.conf\";\n"
        "APT::Default-Release \"bootstrap\";\n");
    write_text(
        root / "parts" / "10-base",
        "APT::Default-Release \"fragment-base\";\n");
    write_text(
        root / "parts" / "20-policy.conf",
        "APT::Default-Release \"fragment-conf\";\n"
        "APT::Machine-ID \"fixture-machine\";\n");
    write_text(
        root / "parts" / "30-ignored.bak",
        "APT::Default-Release \"ignored\";\n");
    write_text(
        root / "main.conf",
        "APT::Default-Release \"main\";\n"
        "Binary::apt-get::APT::Default-Release \"binary\";\n"
        "Dir::Etc::Preferences \"preferences\";\n"
        "Dir::Etc::PreferencesParts \"preferences.d\";\n"
        "APT::Get::Never-Include-Phased-Updates \"yes\";\n");

    const char *old_apt_config = std::getenv("APT_CONFIG");
    const std::string old_value =
        old_apt_config == nullptr
            ? std::string{}
            : old_apt_config;
    const bool had_old = old_apt_config != nullptr;
    assert(::setenv(
               "APT_CONFIG",
               (root / "bootstrap.conf").c_str(),
               1) == 0);

    const auto host = DebianAptConfiguration::read(error);

    if (had_old) {
        assert(::setenv(
                   "APT_CONFIG",
                   old_value.c_str(),
                   1) == 0);
    } else {
        assert(::unsetenv("APT_CONFIG") == 0);
    }

    assert(host.has_value());
    assert(error.empty());
    // APT_CONFIG -> fragments (including *.conf) -> main -> Binary::apt-get.
    assert(host->value("APT::Default-Release") ==
           std::optional<std::string>("binary"));
    assert(host->value("APT::Machine-ID") ==
           std::optional<std::string>("fixture-machine"));
    assert(host->value(
               "APT::Get::Never-Include-Phased-Updates") ==
           std::optional<std::string>("yes"));
    assert(host->path("Dir::Etc::Preferences", "preferences") ==
           root / "preferences");
    assert(host->path(
               "Dir::Etc::PreferencesParts",
               "preferences.d") ==
           root / "preferences.d");

    fs::remove_all(root);
    return 0;
}
