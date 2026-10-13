// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_APT_SOURCE_FORMAT_HPP
#define INFILTRATOR_SOFTWARE_APT_SOURCE_FORMAT_HPP

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct AptDeb822Stanza {
    std::size_t number{0U};
    std::size_t start{0U};
    std::size_t end{0U};
    std::size_t next{0U};
    bool valid{true};
    std::map<std::string, std::string> fields;
};

/*
 * Parse deb822-style APT source stanzas once for inventory, mutation and the
 * native repository engine. Byte boundaries refer to the original input so
 * callers that edit a source file can preserve formatting and line endings.
 * Field names are lowered, ordinary continuation lines are unfolded with one
 * separating space, and inline Signed-By continuation lines retain paragraph
 * breaks (including the deb822 '.' blank-line escape) because OpenPGP armour
 * is not an ordinary whitespace-separated source field. Full-line comments
 * are ignored, and malformed/duplicate fields mark only that stanza invalid.
 */
[[nodiscard]] std::vector<AptDeb822Stanza> parse_apt_deb822_stanzas(
    std::string_view content);

[[nodiscard]] bool apt_deb822_contains_word(
    const std::map<std::string, std::string> &fields,
    std::string_view field,
    std::string_view expected);

} // namespace infiltrator::software

#endif
