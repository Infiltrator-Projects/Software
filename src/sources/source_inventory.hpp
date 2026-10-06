// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_SOURCE_INVENTORY_HPP
#define INFILTRATOR_SOFTWARE_SOURCE_INVENTORY_HPP

#include "core/model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

class SourceInventory {
public:
    std::vector<SourceRecord> list(std::string &error) const;

    static std::vector<SourceRecord> parse_apt_list(
        std::string_view content,
        std::string_view backing_file);

    static std::vector<SourceRecord> parse_apt_deb822(
        std::string_view content,
        std::string_view backing_file);
};

} // namespace infiltrator::software

#endif
