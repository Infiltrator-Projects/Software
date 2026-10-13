// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/text.hpp"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using infiltrator::software::text::ascii_lower;
    using infiltrator::software::text::ascii_space;
    using infiltrator::software::text::lower_ascii;
    using infiltrator::software::text::split_words;
    using infiltrator::software::text::trim;

    assert(ascii_space(' '));
    assert(ascii_space('\t'));
    assert(!ascii_space('A'));
    assert(ascii_lower('Z') == 'z');
    assert(ascii_lower('z') == 'z');
    assert(ascii_lower(static_cast<char>(0xC4)) == static_cast<char>(0xC4));

    assert(trim(" \t alpha \r\n") == "alpha");
    assert(lower_ascii("DeBiAn-ASCII") == "debian-ascii");
    assert((split_words("  one\ttwo\nthree  ") ==
            std::vector<std::string>{"one", "two", "three"}));
    return 0;
}
