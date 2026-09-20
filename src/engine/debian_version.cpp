// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_version.hpp"

#include <cctype>
#include <cstddef>
#include <string_view>

namespace infiltrator::software {
namespace {

struct VersionParts {
    std::string_view epoch{"0"};
    std::string_view upstream;
    std::string_view revision{"0"};
};

bool ascii_digit(const char value) noexcept
{
    return value >= '0' && value <= '9';
}

bool all_digits(const std::string_view value) noexcept
{
    if (value.empty()) {
        return false;
    }

    for (const char character : value) {
        if (!ascii_digit(character)) {
            return false;
        }
    }
    return true;
}

VersionParts split_version(const std::string_view version) noexcept
{
    VersionParts parts;
    std::string_view remainder = version;

    const std::size_t colon = version.find(':');
    if (colon != std::string_view::npos) {
        const std::string_view epoch = version.substr(0U, colon);
        if (all_digits(epoch)) {
            parts.epoch = epoch;
            remainder = version.substr(colon + 1U);
        }
    }

    const std::size_t hyphen = remainder.rfind('-');
    if (hyphen == std::string_view::npos) {
        parts.upstream = remainder;
    } else {
        parts.upstream = remainder.substr(0U, hyphen);
        parts.revision = remainder.substr(hyphen + 1U);
    }

    return parts;
}

std::string_view strip_leading_zeroes(
    std::string_view value) noexcept
{
    std::size_t index = 0U;
    while (index < value.size() && value[index] == '0') {
        ++index;
    }
    return value.substr(index);
}

int compare_epoch(
    const std::string_view left,
    const std::string_view right) noexcept
{
    const std::string_view left_value = strip_leading_zeroes(left);
    const std::string_view right_value = strip_leading_zeroes(right);

    if (left_value.size() != right_value.size()) {
        return left_value.size() < right_value.size() ? -1 : 1;
    }

    const int lexical = left_value.compare(right_value);
    if (lexical < 0) {
        return -1;
    }
    if (lexical > 0) {
        return 1;
    }
    return 0;
}

// Debian's non-digit ordering is deliberately not ASCII ordering:
//   ~ sorts before everything, including end-of-string;
//   end-of-string sorts before every other character;
//   letters sort before punctuation;
//   punctuation then sorts by byte value.
int non_digit_order(const char value) noexcept
{
    if (value == '~') {
        return -1;
    }
    if (value == '\0') {
        return 0;
    }

    const unsigned char byte =
        static_cast<unsigned char>(value);
    if (std::isalpha(byte) != 0) {
        return static_cast<int>(byte);
    }
    if (ascii_digit(value)) {
        return 0;
    }
    return static_cast<int>(byte) + 256;
}

char character_at(
    const std::string_view value,
    const std::size_t index) noexcept
{
    return index < value.size() ? value[index] : '\0';
}

int compare_revision_part(
    const std::string_view left,
    const std::string_view right) noexcept
{
    std::size_t left_index = 0U;
    std::size_t right_index = 0U;

    while (left_index < left.size() ||
           right_index < right.size()) {
        while (
            (left_index < left.size() &&
             !ascii_digit(left[left_index])) ||
            (right_index < right.size() &&
             !ascii_digit(right[right_index]))) {
            const int left_order =
                non_digit_order(character_at(left, left_index));
            const int right_order =
                non_digit_order(character_at(right, right_index));

            if (left_order != right_order) {
                return left_order < right_order ? -1 : 1;
            }

            if (left_index < left.size()) {
                ++left_index;
            }
            if (right_index < right.size()) {
                ++right_index;
            }
        }

        while (
            left_index < left.size() &&
            left[left_index] == '0') {
            ++left_index;
        }
        while (
            right_index < right.size() &&
            right[right_index] == '0') {
            ++right_index;
        }

        int first_difference = 0;
        while (
            left_index < left.size() &&
            right_index < right.size() &&
            ascii_digit(left[left_index]) &&
            ascii_digit(right[right_index])) {
            if (first_difference == 0) {
                first_difference =
                    static_cast<int>(
                        static_cast<unsigned char>(
                            left[left_index])) -
                    static_cast<int>(
                        static_cast<unsigned char>(
                            right[right_index]));
            }
            ++left_index;
            ++right_index;
        }

        if (left_index < left.size() &&
            ascii_digit(left[left_index])) {
            return 1;
        }
        if (right_index < right.size() &&
            ascii_digit(right[right_index])) {
            return -1;
        }
        if (first_difference < 0) {
            return -1;
        }
        if (first_difference > 0) {
            return 1;
        }
    }

    return 0;
}

} // namespace

int compare_debian_versions(
    const std::string_view left,
    const std::string_view right) noexcept
{
    const VersionParts left_parts = split_version(left);
    const VersionParts right_parts = split_version(right);

    const int epoch =
        compare_epoch(left_parts.epoch, right_parts.epoch);
    if (epoch != 0) {
        return epoch;
    }

    const int upstream =
        compare_revision_part(
            left_parts.upstream,
            right_parts.upstream);
    if (upstream != 0) {
        return upstream;
    }

    return compare_revision_part(
        left_parts.revision,
        right_parts.revision);
}

bool debian_version_is_newer(
    const std::string_view candidate,
    const std::string_view installed) noexcept
{
    return compare_debian_versions(candidate, installed) > 0;
}

} // namespace infiltrator::software
