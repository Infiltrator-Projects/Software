// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/source_mutation.hpp"

#include "sources/apt_source_format.hpp"
#include "core/text.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

using text::trim;
using text::split_words;

std::string_view without_cr(std::string_view value)
{
    if (!value.empty() && value.back() == '\r') {
        value.remove_suffix(1U);
    }
    return value;
}

std::size_t indentation_end(std::string_view line)
{
    std::size_t index = 0U;
    while (index < line.size() &&
           (line[index] == ' ' || line[index] == '\t')) {
        ++index;
    }
    return index;
}

bool is_deb_entry(std::string_view value)
{
    return value.rfind("deb ", 0U) == 0U ||
           value.rfind("deb-src ", 0U) == 0U;
}

bool find_deb822_field(
    std::string_view block,
    std::string_view field,
    std::size_t &value_start,
    std::size_t &line_end)
{
    std::size_t start = 0U;
    while (start <= block.size()) {
        const std::size_t newline = block.find('\n', start);
        const std::size_t end =
            newline == std::string_view::npos
                ? block.size()
                : newline;
        std::string_view line =
            without_cr(block.substr(start, end - start));
        if (line.size() >= field.size()) {
            bool matches = true;
            for (std::size_t index = 0U;
                 index < field.size();
                 ++index) {
                if (std::tolower(
                        static_cast<unsigned char>(line[index])) !=
                    std::tolower(
                        static_cast<unsigned char>(field[index]))) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                value_start = start;
                line_end = end;
                return true;
            }
        }
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1U;
    }
    return false;
}

bool is_deb822_source(
    const std::map<std::string, std::string> &fields)
{
    if (fields.find("uris") == fields.end() ||
        fields.find("suites") == fields.end()) {
        return false;
    }
    if (fields.find("types") == fields.end()) {
        return true;
    }
    return apt_deb822_contains_word(fields, "types", "deb") ||
           apt_deb822_contains_word(fields, "types", "deb-src");
}

bool list_identity_matches(
    const std::string_view body,
    const std::string_view expected_uri,
    const std::string_view expected_suites)
{
    const std::vector<std::string> words = split_words(body);
    if (words.size() < 3U) return false;

    std::size_t index = 1U;
    if (index < words.size() &&
        !words[index].empty() &&
        words[index].front() == '[') {
        while (index < words.size() &&
               words[index].find(']') == std::string::npos) {
            ++index;
        }
        if (index < words.size()) ++index;
    }
    return index + 1U < words.size() &&
           words[index] == expected_uri &&
           words[index + 1U] == expected_suites;
}

bool deb822_identity_matches(
    const std::map<std::string, std::string> &fields,
    const std::string_view expected_uri,
    const std::string_view expected_suites)
{
    const auto uris = fields.find("uris");
    const auto suites = fields.find("suites");
    if (uris == fields.end() || suites == fields.end()) {
        return false;
    }

    bool uri_matches = false;
    for (const std::string &uri : split_words(uris->second)) {
        if (uri == expected_uri) {
            uri_matches = true;
            break;
        }
    }
    return uri_matches &&
           trim(suites->second) == trim(expected_suites);
}

} // namespace

bool set_apt_list_entry_enabled(
    std::string_view content,
    const std::size_t line_number,
    const bool enabled,
    const std::string_view expected_uri,
    const std::string_view expected_suites,
    std::string &updated,
    std::string &error)
{
    const std::string stable_content(content);
    content = stable_content;
    updated.clear();
    error.clear();
    if (line_number == 0U) {
        error = "APT source line number must be greater than zero.";
        return false;
    }

    std::size_t start = 0U;
    std::size_t current = 1U;
    while (current < line_number) {
        const std::size_t newline = content.find('\n', start);
        if (newline == std::string_view::npos) {
            error = "APT source line no longer exists.";
            return false;
        }
        start = newline + 1U;
        ++current;
    }

    const std::size_t newline = content.find('\n', start);
    const std::size_t end =
        newline == std::string_view::npos
            ? content.size()
            : newline;
    std::string_view raw = content.substr(start, end - start);
    const bool had_cr = !raw.empty() && raw.back() == '\r';
    raw = without_cr(raw);

    const std::size_t indent_end = indentation_end(raw);
    const std::string_view indent = raw.substr(0U, indent_end);
    std::string_view body = raw.substr(indent_end);

    bool currently_enabled = true;
    if (!body.empty() && body.front() == '#') {
        currently_enabled = false;
        body.remove_prefix(1U);
        while (!body.empty() &&
               (body.front() == ' ' || body.front() == '\t')) {
            body.remove_prefix(1U);
        }
    }

    if (!is_deb_entry(body)) {
        error = "Selected APT source line is not a deb or deb-src entry.";
        return false;
    }
    if (expected_uri.empty() ||
        expected_suites.empty() ||
        !list_identity_matches(
            body, expected_uri, expected_suites)) {
        error =
            "Selected APT source changed since it was reviewed; refresh the "
            "repository list before changing it.";
        return false;
    }

    if (currently_enabled == enabled) {
        updated.assign(content);
        return true;
    }

    std::string replacement(indent);
    if (!enabled) {
        replacement += "# ";
    }
    replacement.append(body);
    if (had_cr) {
        replacement.push_back('\r');
    }

    updated.assign(content);
    updated.replace(start, end - start, replacement);
    return true;
}

bool set_apt_deb822_entry_enabled(
    std::string_view content,
    const std::size_t stanza_number,
    const bool enabled,
    const std::string_view expected_uri,
    const std::string_view expected_suites,
    std::string &updated,
    std::string &error)
{
    const std::string stable_content(content);
    content = stable_content;
    updated.clear();
    error.clear();
    if (stanza_number == 0U) {
        error = "APT source stanza number must be greater than zero.";
        return false;
    }

    const std::vector<AptDeb822Stanza> stanzas =
        parse_apt_deb822_stanzas(content);
    const auto found = std::find_if(
        stanzas.begin(),
        stanzas.end(),
        [&](const AptDeb822Stanza &stanza) {
            return stanza.number == stanza_number;
        });
    if (found == stanzas.end()) {
        error = "APT source stanza no longer exists.";
        return false;
    }
    if (!found->valid || !is_deb822_source(found->fields)) {
        error =
            "Selected deb822 stanza is not a valid Debian repository.";
        return false;
    }
    if (expected_uri.empty() ||
        expected_suites.empty() ||
        !deb822_identity_matches(
            found->fields,
            expected_uri,
            expected_suites)) {
        error =
            "Selected APT source changed since it was reviewed; refresh "
            "the repository list before changing it.";
        return false;
    }

    const std::string_view block =
        content.substr(found->start, found->end - found->start);
    std::string replacement(block);
    std::size_t field_start = 0U;
    std::size_t field_end = 0U;
    if (find_deb822_field(
            block,
            "Enabled:",
            field_start,
            field_end)) {
        const bool had_cr =
            field_end > field_start &&
            block[field_end - 1U] == '\r';
        std::string line =
            enabled ? "Enabled: yes" : "Enabled: no";
        if (had_cr) {
            line.push_back('\r');
        }
        replacement.replace(
            field_start,
            field_end - field_start,
            line);
    } else {
        const std::string_view line_break =
            block.find("\r\n") != std::string_view::npos
                ? std::string_view{"\r\n"}
                : std::string_view{"\n"};
        const bool had_trailing_line_break =
            !replacement.empty() && replacement.back() == '\n';
        if (!had_trailing_line_break) {
            replacement.append(line_break);
        }
        replacement += enabled ? "Enabled: yes" : "Enabled: no";
        if (had_trailing_line_break) {
            /*
             * The parser leaves stanza-separator whitespace outside this
             * block. Re-emit the consumed final data-line terminator after
             * the inserted field so the following blank separator remains
             * blank instead of being collapsed into a single newline.
             */
            replacement.append(line_break);
        }
    }

    updated.assign(content);
    updated.replace(
        found->start,
        found->end - found->start,
        replacement);
    return true;
}

} // namespace infiltrator::software
