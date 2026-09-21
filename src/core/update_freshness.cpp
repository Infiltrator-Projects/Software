// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_freshness.hpp"

namespace infiltrator::software {

bool update_metadata_refresh_due(
    const std::int64_t now_monotonic_us,
    const std::int64_t last_success_monotonic_us,
    const bool refresh_busy) noexcept
{
    if (refresh_busy) {
        return false;
    }
    if (last_success_monotonic_us <= 0) {
        return true;
    }
    if (now_monotonic_us <= last_success_monotonic_us) {
        return false;
    }
    return now_monotonic_us - last_success_monotonic_us >=
           update_metadata_refresh_interval_us;
}

} // namespace infiltrator::software
