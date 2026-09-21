// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_freshness.hpp"

#include <cassert>

int main()
{
    using infiltrator::software::update_metadata_refresh_due;
    using infiltrator::software::update_metadata_refresh_interval_us;

    assert(update_metadata_refresh_due(1, 0, false));
    assert(!update_metadata_refresh_due(
        update_metadata_refresh_interval_us, 1, false));
    assert(update_metadata_refresh_due(
        update_metadata_refresh_interval_us + 1, 1, false));
    assert(!update_metadata_refresh_due(
        update_metadata_refresh_interval_us * 10,
        1,
        true));
    assert(!update_metadata_refresh_due(100, 100, false));
    assert(!update_metadata_refresh_due(99, 100, false));
    return 0;
}
