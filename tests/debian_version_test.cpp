// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_version.hpp"

#include <cassert>
#include <string_view>

namespace {

void older(
    const std::string_view left,
    const std::string_view right)
{
    using infiltrator::software::compare_debian_versions;
    assert(compare_debian_versions(left, right) < 0);
    assert(compare_debian_versions(right, left) > 0);
}

void equal(
    const std::string_view left,
    const std::string_view right)
{
    using infiltrator::software::compare_debian_versions;
    assert(compare_debian_versions(left, right) == 0);
    assert(compare_debian_versions(right, left) == 0);
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    equal("1.0", "1.0");
    equal("1.01", "1.1");
    equal("1.0", "1.0-0");
    equal("001:1.0", "1:1.0");

    older("1.0~rc1", "1.0");
    older("1.0~~", "1.0~");
    older("1.0-1~exp1", "1.0-1");
    older("1.0", "1.0+git1");
    older("1.0a", "1.0+");
    older("1.0-1", "1.0-2");
    older("1.0-1", "1.0-1+b1");
    older("1.0-1", "1.0-1.1");
    older("1:9.9", "2:1.0");

    older(
        "999999999999999999999999:9.0",
        "1000000000000000000000000:1.0");

    assert(debian_version_is_newer("2.0", "1.9"));
    assert(!debian_version_is_newer("1.0~beta1", "1.0"));
    assert(!debian_version_is_newer("1.0-0", "1.0"));

    return 0;
}
