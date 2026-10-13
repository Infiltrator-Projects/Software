// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_CONTROL_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_CONTROL_HPP

#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace infiltrator::software {

using DebianControlFields = std::map<std::string, std::string>;
using DebianControlRecordCallback =
    std::function<void(const DebianControlFields &)>;

/*
 * Parse Debian control paragraphs with one canonical set of continuation,
 * whitespace, field-name and duplicate-field rules. Package indexes and dpkg
 * installed state intentionally share this parser so their interpretation of
 * the same Debian format cannot drift independently.
 */
[[nodiscard]] bool parse_debian_control_records(
    std::string_view content,
    const DebianControlRecordCallback &record,
    std::string &error);

} // namespace infiltrator::software

#endif
