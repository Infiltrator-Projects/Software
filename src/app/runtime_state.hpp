// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_RUNTIME_STATE_HPP
#define INFILTRATOR_SOFTWARE_RUNTIME_STATE_HPP

#include <filesystem>
#include <string_view>

namespace infiltrator::software::app {

std::filesystem::path software_update_runtime_state_path();
void set_software_update_runtime_state(std::string_view value);

} // namespace infiltrator::software::app

#endif
