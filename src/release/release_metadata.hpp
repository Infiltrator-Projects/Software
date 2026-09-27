// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace infiltrator::software::release {

inline std::string normalize_edition(std::string value)
{
    const auto first=value.find_first_not_of(" \t\r\n");
    if (first==std::string::npos) return {};
    const auto last=value.find_last_not_of(" \t\r\n");
    value=value.substr(first,last-first+1U);
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline bool supported_edition(std::string_view configured,std::string_view edition)
{
    // Mint's upgrade-info editions are lower case; /etc/linuxmint/info may
    // spell EDITION with capitals. Accept comma or whitespace separators.
    std::string token;
    for (const unsigned char c:configured) {
        if (c==',' || std::isspace(c)) {
            if (!token.empty() && normalize_edition(token)==edition) return true;
            token.clear();
        } else token.push_back(static_cast<char>(c));
    }
    return !token.empty() && normalize_edition(token)==edition;
}

inline std::string meta_package(std::string_view edition)
{
    if (edition.empty()) return {};
    for (const unsigned char c:edition)
        if (!std::islower(c) && !std::isdigit(c) && c!='-') return {};
    return "mint-meta-"+std::string(edition);
}
}
