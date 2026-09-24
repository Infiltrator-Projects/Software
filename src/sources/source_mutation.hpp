// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_SOURCE_MUTATION_HPP
#define INFILTRATOR_SOFTWARE_SOURCE_MUTATION_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace infiltrator::software {

bool set_apt_list_entry_enabled(
    std::string_view content,
    std::size_t line_number,
    bool enabled,
    std::string &updated,
    std::string &error);

bool set_apt_deb822_entry_enabled(
    std::string_view content,
    std::size_t stanza_number,
    bool enabled,
    std::string &updated,
    std::string &error);

} // namespace infiltrator::software

#endif
