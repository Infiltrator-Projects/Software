// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_package_index.hpp"

#include "core/text.hpp"
#include "engine/debian_control.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

using Fields = DebianControlFields;

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
    const auto found = fields.find(text::lower_ascii(std::string(name)));
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
                text::trim(std::string_view(source_field).substr(
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
    package.supported = field(fields, "supported");
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
    if (!parse_debian_control_records(
            content,
            [&](const DebianControlFields &fields) {
                append_record(fields, source_id, packages);
            },
            error)) {
        return {};
    }
    return packages;
}

} // namespace infiltrator::software
