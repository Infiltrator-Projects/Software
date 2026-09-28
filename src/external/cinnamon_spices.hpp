// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_CINNAMON_SPICES_HPP
#define INFILTRATOR_SOFTWARE_CINNAMON_SPICES_HPP

#include "external/external_updates.hpp"

#include <string>
#include <vector>

namespace infiltrator::software {

bool discover_native_cinnamon_updates(
    std::vector<ExternalUpdate> &updates,
    std::string &error);

bool apply_native_cinnamon_updates_selected(
    const std::vector<ExternalUpdate> &selected,
    std::string &error,
    ExternalProgressCallback progress = {},
    std::vector<ExternalUpdate> *completed = nullptr);

} // namespace infiltrator::software

#endif
