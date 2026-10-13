// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/apt_source_format.hpp"

#include "core/text.hpp"

#include <utility>

namespace infiltrator::software {
namespace {

std::string_view without_cr(std::string_view value)
{
    if (!value.empty() && value.back() == '\r') {
        value.remove_suffix(1U);
    }
    return value;
}

std::size_t line_after(
    const std::string_view content,
    const std::size_t newline)
{
    return newline == std::string_view::npos
        ? content.size()
        : newline + 1U;
}

bool blank_line(const std::string_view line)
{
    return text::trim(without_cr(line)).empty();
}

} // namespace

std::vector<AptDeb822Stanza> parse_apt_deb822_stanzas(
    const std::string_view content)
{
    std::vector<AptDeb822Stanza> result;
    std::size_t cursor = 0U;
    std::size_t stanza_number = 0U;

    while (cursor < content.size()) {
        /* Ignore separator whitespace without manufacturing empty stanzas. */
        for (;;) {
            const std::size_t newline = content.find('\n', cursor);
            const std::size_t end =
                newline == std::string_view::npos
                    ? content.size()
                    : newline;
            if (!blank_line(content.substr(cursor, end - cursor))) {
                break;
            }
            cursor = line_after(content, newline);
            if (cursor >= content.size()) {
                return result;
            }
        }

        AptDeb822Stanza stanza;
        stanza.number = ++stanza_number;
        stanza.start = cursor;
        std::string current_key;
        std::size_t line_start = cursor;

        for (;;) {
            const std::size_t newline = content.find('\n', line_start);
            const std::size_t line_end =
                newline == std::string_view::npos
                    ? content.size()
                    : newline;
            const std::string_view line =
                without_cr(content.substr(
                    line_start,
                    line_end - line_start));

            if (blank_line(line)) {
                stanza.end = line_start;
                stanza.next = line_after(content, newline);
                break;
            }

            const std::string clean = text::trim(line);
            if (!clean.empty() && clean.front() == '#') {
                /* Full-line comments are not fields and do not break a value. */
            } else if (!line.empty() &&
                       (line.front() == ' ' || line.front() == '\t')) {
                if (current_key.empty()) {
                    stanza.valid = false;
                } else {
                    const std::string continuation = text::trim(line);
                    if (!continuation.empty()) {
                        std::string &value = stanza.fields[current_key];
                        if (!value.empty()) {
                            value.push_back(' ');
                        }
                        value += continuation;
                    }
                }
            } else {
                const std::size_t colon = line.find(':');
                if (colon == std::string_view::npos) {
                    stanza.valid = false;
                    current_key.clear();
                } else {
                    const std::string key = text::lower_ascii(
                        text::trim(line.substr(0U, colon)));
                    if (key.empty()) {
                        stanza.valid = false;
                        current_key.clear();
                    } else {
                        const auto inserted = stanza.fields.emplace(
                            key,
                            text::trim(line.substr(colon + 1U)));
                        if (!inserted.second) {
                            stanza.valid = false;
                            current_key.clear();
                        } else {
                            current_key = key;
                        }
                    }
                }
            }

            if (newline == std::string_view::npos) {
                stanza.end = content.size();
                stanza.next = content.size();
                break;
            }
            line_start = newline + 1U;
        }

        result.emplace_back(std::move(stanza));
        cursor = result.back().next;
    }

    return result;
}

bool apt_deb822_contains_word(
    const std::map<std::string, std::string> &fields,
    const std::string_view field,
    const std::string_view expected)
{
    const auto found = fields.find(std::string(field));
    if (found == fields.end()) {
        return false;
    }
    for (const std::string &word : text::split_words(found->second)) {
        if (word == expected) {
            return true;
        }
    }
    return false;
}

} // namespace infiltrator::software
