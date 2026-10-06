// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_TEXT_UTILS_HPP
#define INFILTRATOR_SOFTWARE_TEXT_UTILS_HPP

#include <cctype>
#include <string>

namespace infiltrator::software::app {

inline std::string single_line(std::string value)
{
    bool previous_space = false;
    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        const bool whitespace =
            ch == '\r' || ch == '\n' || ch == '\t' ||
            std::isspace(byte) != 0;
        if (whitespace) {
            if (!normalized.empty() && !previous_space) {
                normalized.push_back(' ');
            }
            previous_space = true;
            continue;
        }
        normalized.push_back(ch);
        previous_space = false;
    }
    while (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

} // namespace infiltrator::software::app

#endif
