// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_INSTALLED_STATE_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_INSTALLED_STATE_HPP

#include "core/model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

class DebianInstalledState final {
public:
    [[nodiscard]] static bool available() noexcept;

    static std::vector<PackageRecord> read(std::string &error);

    static std::vector<PackageRecord> parse(
        std::string_view content, std::string &error);
};

} // namespace infiltrator::software

#endif
