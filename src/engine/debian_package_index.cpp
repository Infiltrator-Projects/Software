// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_package_index.hpp"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

using Fields = std::map<std::string, std::string>;

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

std::uint64_t parse_unsigned(
    const std::string_view text,
    const std::uint64_t multiplier = 1U)
{
    if (text.empty()) {
        return 0U;
    }

    std::uint64_t value = 0U;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        return 0U;
    }

    if (multiplier != 0U &&
        value > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return value * multiplier;
}

std::string field(
    const Fields &fields,
    const std::string_view name)
{
    const auto found = fields.find(std::string(name));
    return found == fields.end() ? std::string{} : found->second;
}

bool yes(const std::string_view value)
{
    return value == "yes" || value == "Yes" || value == "true" ||
           value == "True" || value == "1";
}

void append_record(
    const Fields &fields,
    const std::string_view source_id,
    std::vector<DebianPackageVersion> &packages)
{
    DebianPackageVersion package;
    package.package = field(fields, "Package");
    package.version = field(fields, "Version");
    package.architecture = field(fields, "Architecture");

    if (package.package.empty() ||
        package.version.empty() ||
        package.architecture.empty()) {
        return;
    }

    package.filename = field(fields, "Filename");
    package.sha256 = field(fields, "SHA256");
    package.source = source_id.empty()
        ? field(fields, "Source")
        : std::string(source_id);
    package.priority = field(fields, "Priority");
    package.multi_arch = field(fields, "Multi-Arch");
    package.depends = field(fields, "Depends");
    package.pre_depends = field(fields, "Pre-Depends");
    package.recommends = field(fields, "Recommends");
    package.provides = field(fields, "Provides");
    package.conflicts = field(fields, "Conflicts");
    package.breaks = field(fields, "Breaks");
    package.replaces = field(fields, "Replaces");
    package.description = field(fields, "Description");
    package.size_bytes =
        parse_unsigned(field(fields, "Size"));
    package.installed_size_bytes =
        parse_unsigned(field(fields, "Installed-Size"), 1024U);
    package.essential = yes(field(fields, "Essential"));

    packages.emplace_back(std::move(package));
}

} // namespace

std::vector<DebianPackageVersion> DebianPackageIndex::read_file(
    const std::string &path,
    const std::string_view source_id,
    std::string &error)
{
    error.clear();

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Unable to read Debian Packages index: " + path;
        return {};
    }

    std::ostringstream content;
    content << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error = "Unable to finish reading Debian Packages index: " + path;
        return {};
    }

    return parse(content.str(), source_id, error);
}

std::vector<DebianPackageVersion> DebianPackageIndex::parse(
    const std::string_view content,
    const std::string_view source_id,
    std::string &error)
{
    error.clear();
    std::vector<DebianPackageVersion> packages;
    Fields fields;
    std::string current_key;

    auto flush = [&]() {
        if (!fields.empty()) {
            append_record(fields, source_id, packages);
        }
        fields.clear();
        current_key.clear();
    };

    std::size_t start = 0U;
    while (start <= content.size()) {
        const std::size_t newline = content.find('\n', start);
        const std::size_t end =
            newline == std::string_view::npos ? content.size() : newline;

        std::string_view line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }

        if (line.empty()) {
            flush();
        } else if (
            (line.front() == ' ' || line.front() == '\t') &&
            !current_key.empty()) {
            if (!fields[current_key].empty()) {
                fields[current_key] += "\n";
            }
            fields[current_key] += trim(line);
        } else {
            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                current_key.clear();
            } else {
                current_key = trim(line.substr(0U, colon));
                if (!current_key.empty()) {
                    fields[current_key] =
                        trim(line.substr(colon + 1U));
                }
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1U;
    }

    flush();
    return packages;
}

} // namespace infiltrator::software
