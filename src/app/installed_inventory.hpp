// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_INSTALLED_INVENTORY_HPP
#define INFILTRATOR_SOFTWARE_INSTALLED_INVENTORY_HPP

#include "core/model.hpp"

#include <string>
#include <vector>

namespace infiltrator::software {

std::vector<PackageRecord> read_installed_packages(
    std::string &error,
    bool *from_engine = nullptr);

} // namespace infiltrator::software

#endif
