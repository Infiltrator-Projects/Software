// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_SOURCE_CONFIGURATION_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_SOURCE_CONFIGURATION_HPP

#include "engine/debian_repository.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

class DebianSourceConfiguration final {
public:
    static std::vector<DebianRepositorySource> read(std::string &error);

    static std::vector<DebianRepositorySource> parse_list(
        std::string_view content,
        std::string_view origin,
        std::string &error);

    static std::vector<DebianRepositorySource> parse_deb822(
        std::string_view content,
        std::string_view origin,
        std::string &error);
};

} // namespace infiltrator::software

#endif
