// SPDX-License-Identifier: GPL-3.0-or-later
#include "release/release_metadata.hpp"

#include <iostream>
#include <string_view>

namespace {

bool expect(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "release metadata check failed: " << description << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    using namespace infiltrator::software::release;

    bool ok = true;
    ok &= expect(normalize_edition("  Cinnamon  ") == "cinnamon",
                 "Cinnamon edition normalization");
    ok &= expect(normalize_edition("MATE") == "mate",
                 "MATE edition normalization");
    ok &= expect(supported_edition("cinnamon,mate,xfce", "cinnamon"),
                 "comma-separated supported edition");
    ok &= expect(supported_edition("mate xfce", "xfce"),
                 "space-separated supported edition");
    ok &= expect(!supported_edition("mate,xfce", "cinnamon"),
                 "unsupported edition rejection");
    ok &= expect(!supported_edition("cinnamonish", "cinnamon"),
                 "partial edition-name rejection");
    ok &= expect(meta_package("cinnamon") == "mint-meta-cinnamon",
                 "Cinnamon meta-package mapping");
    ok &= expect(meta_package("").empty(),
                 "empty edition rejection");
    ok &= expect(meta_package("../bad").empty(),
                 "unsafe edition rejection");

    return ok ? 0 : 1;
}
