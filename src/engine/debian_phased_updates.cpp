// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_phased_updates.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

std::string trim(const std::string_view value)
{
    std::size_t first = 0U;
    while (first < value.size() &&
           std::isspace(
               static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(
               static_cast<unsigned char>(value[last - 1U])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string lower_ascii(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

bool bool_value(
    const std::string_view value,
    bool &parsed)
{
    const std::string text = lower_ascii(trim(value));
    if (text == "1" || text == "true" ||
        text == "yes" || text == "on") {
        parsed = true;
        return true;
    }
    if (text == "0" || text == "false" ||
        text == "no" || text == "off") {
        parsed = false;
        return true;
    }
    return false;
}

std::string unquote(std::string value)
{
    value = trim(value);
    if (value.size() >= 2U &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value =
            value.substr(1U, value.size() - 2U);
    }
    return value;
}

void parse_config_line(
    std::string line,
    std::string &machine_id,
    bool &always_include,
    bool &never_include)
{
    const std::size_t slash = line.find("//");
    const std::size_t hash = line.find('#');
    std::size_t comment = std::string::npos;
    if (slash != std::string::npos) {
        comment = slash;
    }
    if (hash != std::string::npos) {
        comment =
            comment == std::string::npos
                ? hash
                : std::min(comment, hash);
    }
    if (comment != std::string::npos) {
        line.erase(comment);
    }

    line = trim(line);
    if (line.empty()) {
        return;
    }
    if (!line.empty() && line.back() == ';') {
        line.pop_back();
    }

    const std::size_t split =
        line.find_first_of(" \t");
    if (split == std::string::npos) {
        return;
    }

    const std::string key =
        lower_ascii(line.substr(0U, split));
    const std::string value =
        unquote(line.substr(split + 1U));

    if (key == "apt::machine-id") {
        machine_id = value;
        return;
    }

    bool parsed = false;
    if (!bool_value(value, parsed)) {
        return;
    }

    if (key ==
        "apt::get::always-include-phased-updates") {
        always_include = parsed;
    } else if (
        key ==
        "update-manager::always-include-phased-updates") {
        always_include = parsed;
    } else if (
        key ==
        "apt::get::never-include-phased-updates") {
        never_include = parsed;
    } else if (
        key ==
        "update-manager::never-include-phased-updates") {
        never_include = parsed;
    }
}

bool readable_config_file(
    const std::filesystem::path &path)
{
    const std::string name = path.filename().string();
    if (name.empty() || name.front() == '.' ||
        name.back() == '~') {
        return false;
    }
    for (const unsigned char ch : name) {
        if (std::isalnum(ch) != 0 ||
            ch == '-' || ch == '_') {
            continue;
        }
        return false;
    }
    return true;
}

void read_config_file(
    const std::filesystem::path &path,
    std::string &machine_id,
    bool &always_include,
    bool &never_include)
{
    std::ifstream input(path);
    if (!input) {
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        parse_config_line(
            std::move(line),
            machine_id,
            always_include,
            never_include);
    }
}

std::string read_machine_id()
{
    std::ifstream input("/etc/machine-id");
    std::string value;
    if (input) {
        std::getline(input, value);
    }
    return trim(value);
}

} // namespace

DebianPhasedUpdatesPolicy
DebianPhasedUpdatesPolicy::read(std::string &error)
{
    error.clear();

    std::string machine_id;
    bool always_include = false;
    bool never_include = false;

    read_config_file(
        "/etc/apt/apt.conf",
        machine_id,
        always_include,
        never_include);

    std::error_code ec;
    const std::filesystem::path directory{
        "/etc/apt/apt.conf.d"};
    if (std::filesystem::is_directory(directory, ec) && !ec) {
        std::vector<std::filesystem::path> files;
        for (const auto &entry :
             std::filesystem::directory_iterator(
                 directory, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_regular_file(ec) &&
                !ec &&
                readable_config_file(entry.path())) {
                files.emplace_back(entry.path());
            }
            ec.clear();
        }
        std::sort(files.begin(), files.end());
        for (const auto &path : files) {
            read_config_file(
                path,
                machine_id,
                always_include,
                never_include);
        }
    }
    if (ec) {
        error =
            "Unable to enumerate APT configuration for phased updates: " +
            ec.message();
        return {};
    }

    if (machine_id.empty()) {
        machine_id = read_machine_id();
    }

    return for_machine(
        std::move(machine_id),
        always_include,
        never_include);
}

DebianPhasedUpdatesPolicy
DebianPhasedUpdatesPolicy::for_machine(
    std::string machine_id,
    const bool always_include,
    const bool never_include)
{
    DebianPhasedUpdatesPolicy policy;
    policy.machine_id_ = std::move(machine_id);
    policy.always_include_ = always_include;
    policy.never_include_ = never_include;
    return policy;
}

std::optional<DebianPolicyDecision>
DebianPhasedUpdatesPolicy::evaluate(
    const DebianPackageVersion &package) const
{
    if (package.phased_update_percentage < 0 ||
        package.phased_update_percentage >= 100 ||
        always_include_) {
        return std::nullopt;
    }

    if (never_include_) {
        return DebianPolicyDecision{
            1,
            std::string(id()),
            "Phased update is held until rollout reaches 100%."};
    }

    if (machine_id_.empty() ||
        std::getenv("SOURCE_DATE_EPOCH") != nullptr) {
        return std::nullopt;
    }

    const std::string source_package =
        package.source_package.empty()
            ? package.package
            : package.source_package;
    const std::string source_version =
        package.source_version.empty()
            ? package.version
            : package.source_version;
    const std::string seed_text =
        source_package + "-" +
        source_version + "-" +
        machine_id_;

    std::seed_seq seed(
        seed_text.begin(),
        seed_text.end());
    std::minstd_rand generator(seed);
    std::uniform_int_distribution<unsigned int>
        distribution(0U, 100U);
    const unsigned int bucket =
        distribution(generator);

    if (bucket <=
        static_cast<unsigned int>(
            package.phased_update_percentage)) {
        return std::nullopt;
    }

    return DebianPolicyDecision{
        1,
        std::string(id()),
        "Held back by Debian/Ubuntu phased rollout (" +
            std::to_string(
                package.phased_update_percentage) +
            "% rollout; this machine bucket " +
            std::to_string(bucket) + ")."};
}

} // namespace infiltrator::software
