// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_SYSTEM_DATA_HPP
#define INFILTRATOR_SOFTWARE_SYSTEM_DATA_HPP

#include "core/model.hpp"

#include <string>
#include <vector>

namespace infiltrator::software::app {

struct SystemDataResult {
    std::vector<PackageRecord> components;
    std::vector<PackageRecord> updates;
    std::string error;
    std::string update_warning;
    bool from_engine{false};
};

SystemDataResult load_system_data(bool refresh_metadata);

} // namespace infiltrator::software::app

#endif
