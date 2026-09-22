// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software::helper {

bool validate_apt_simulation(
    const std::vector<std::string> &approved_specs,
    std::string_view simulation_output,
    std::string &error);

} // namespace infiltrator::software::helper
