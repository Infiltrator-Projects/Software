// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/engine_compatibility.hpp"

#include <cassert>

using infiltrator::software::EngineVersionRelation;
using infiltrator::software::compare_engine_version;
using infiltrator::software::engine_version_is_compatible_with_client;

int main()
{
    assert(compare_engine_version("0.3.49", "0.3.49") ==
           EngineVersionRelation::same);
    assert(compare_engine_version("0.3.50", "0.3.49") ==
           EngineVersionRelation::newer);
    assert(compare_engine_version("0.4.0", "0.3.99") ==
           EngineVersionRelation::newer);
    assert(compare_engine_version("1.0.0", "0.99.99") ==
           EngineVersionRelation::newer);
    assert(compare_engine_version("0.3.48", "0.3.49") ==
           EngineVersionRelation::older);

    assert(engine_version_is_compatible_with_client(
        "0.3.49", "0.3.49"));
    assert(engine_version_is_compatible_with_client(
        "0.3.50", "0.3.49"));
    assert(!engine_version_is_compatible_with_client(
        "0.3.48", "0.3.49"));

    assert(compare_engine_version("0.3", "0.3.49") ==
           EngineVersionRelation::invalid);
    assert(compare_engine_version("0.3.49+native", "0.3.49") ==
           EngineVersionRelation::invalid);
    assert(compare_engine_version("", "0.3.49") ==
           EngineVersionRelation::invalid);
    assert(!engine_version_is_compatible_with_client(
        "broken", "0.3.49"));

    return 0;
}
