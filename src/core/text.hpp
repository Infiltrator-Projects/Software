// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_CORE_TEXT_HPP
#define INFILTRATOR_SOFTWARE_CORE_TEXT_HPP

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software::text {

[[nodiscard]] constexpr bool ascii_space(const char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' ||
           value == '\r' || value == '\f' || value == '\v';
}

[[nodiscard]] constexpr char ascii_lower(const char value) noexcept
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

[[nodiscard]] inline std::string trim(const std::string_view value)
{
    std::size_t first = 0U;
    while (first < value.size() && ascii_space(value[first])) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first && ascii_space(value[last - 1U])) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

[[nodiscard]] inline std::string lower_ascii(std::string value)
{
    for (char &character : value) {
        character = ascii_lower(character);
    }
    return value;
}

[[nodiscard]] inline std::vector<std::string> split_words(
    const std::string_view value)
{
    std::vector<std::string> result;
    std::size_t start = 0U;
    while (start < value.size()) {
        while (start < value.size() && ascii_space(value[start])) {
            ++start;
        }
        if (start == value.size()) {
            break;
        }
        std::size_t end = start;
        while (end < value.size() && !ascii_space(value[end])) {
            ++end;
        }
        result.emplace_back(value.substr(start, end - start));
        start = end;
    }
    return result;
}

} // namespace infiltrator::software::text

#endif
