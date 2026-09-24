// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_package_index.hpp"

#include <algorithm>
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

std::string field_key(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
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
    const auto found = fields.find(field_key(std::string(name)));
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
    package.package = field(fields, "package");
    package.version = field(fields, "version");
    package.architecture = field(fields, "architecture");

    if (package.package.empty() ||
        package.version.empty() ||
        package.architecture.empty()) {
        return;
    }

    package.filename = field(fields, "filename");
    package.sha256 = field(fields, "sha256");
    package.source = source_id.empty()
        ? field(fields, "source")
        : std::string(source_id);
    package.priority = field(fields, "priority");

    const std::string source_field = field(fields, "source");
    package.source_package = package.package;
    package.source_version = package.version;
    if (!source_field.empty()) {
        const std::size_t space = source_field.find_first_of(" \t(");
        package.source_package =
            source_field.substr(0U, space);
        const std::size_t open = source_field.find('(');
        const std::size_t close =
            source_field.find(')', open == std::string::npos ? 0U : open + 1U);
        if (open != std::string::npos &&
            close != std::string::npos &&
            close > open + 1U) {
            package.source_version =
                trim(std::string_view(source_field).substr(
                    open + 1U,
                    close - open - 1U));
        }
    }

    const std::string phased_text =
        field(fields, "phased-update-percentage");
    const std::uint64_t phased =
        parse_unsigned(phased_text);
    if (!phased_text.empty() && phased <= 100U) {
        package.phased_update_percentage =
            static_cast<int>(phased);
    }

    package.multi_arch = field(fields, "multi-arch");
    package.depends = field(fields, "depends");
    package.pre_depends = field(fields, "pre-depends");
    package.recommends = field(fields, "recommends");
    package.provides = field(fields, "provides");
    package.conflicts = field(fields, "conflicts");
    package.breaks = field(fields, "breaks");
    package.replaces = field(fields, "replaces");
    package.description = field(fields, "description");
    package.size_bytes =
        parse_unsigned(field(fields, "size"));
    package.installed_size_bytes =
        parse_unsigned(field(fields, "installed-size"), 1024U);
    package.essential = yes(field(fields, "essential"));

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

        if (line.find_first_not_of(" \t") == std::string_view::npos) {
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
                error = "Malformed Debian control line without a field separator.";
                return {};
            } else {
                current_key =
                    field_key(trim(line.substr(0U, colon)));
                if (current_key.empty()) {
                    error = "Debian control field name is empty.";
                    return {};
                }
                const auto inserted = fields.emplace(
                    current_key, trim(line.substr(colon + 1U)));
                if (!inserted.second) {
                    error =
                        "Duplicate Debian control field: " +
                        current_key;
                    return {};
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
