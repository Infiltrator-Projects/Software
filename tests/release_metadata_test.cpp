// SPDX-License-Identifier: GPL-3.0-or-later
#include "release/release_metadata.hpp"

#include <cassert>

int main()
{
    using namespace infiltrator::software::release;
    assert(normalize_edition("  Cinnamon  ")=="cinnamon");
    assert(normalize_edition("MATE")=="mate");
    assert(supported_edition("cinnamon,mate,xfce","cinnamon"));
    assert(supported_edition("mate xfce", "xfce"));
    assert(!supported_edition("mate,xfce", "cinnamon"));
    assert(!supported_edition("cinnamonish", "cinnamon"));
    assert(meta_package("cinnamon")=="mint-meta-cinnamon");
    assert(meta_package("").empty());
    assert(meta_package("../bad").empty());
}
