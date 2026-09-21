// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_UPDATE_FRESHNESS_HPP
#define INFILTRATOR_SOFTWARE_UPDATE_FRESHNESS_HPP

#include <cstdint>

namespace infiltrator::software {

inline constexpr std::int64_t update_metadata_refresh_interval_us =
    INT64_C(60) * INT64_C(1000000);

bool update_metadata_refresh_due(
    std::int64_t now_monotonic_us,
    std::int64_t last_success_monotonic_us,
    bool refresh_busy) noexcept;

} // namespace infiltrator::software

#endif
