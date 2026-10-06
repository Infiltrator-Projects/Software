// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_REPAIR_DIAGNOSTICS_HPP
#define INFILTRATOR_SOFTWARE_REPAIR_DIAGNOSTICS_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace infiltrator::software::app {

struct RepairInspection {
    std::size_t source_count{0U};
    std::size_t update_count{0U};
    bool engine_ready{false};
    bool interrupted{false};
    bool refreshed_metadata{false};
    std::vector<std::string> issues;
};

RepairInspection inspect_repair_state(bool refresh_metadata);

} // namespace infiltrator::software::app

#endif
