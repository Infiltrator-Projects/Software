// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/source_mutation.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace infiltrator::software {
namespace {

std::string_view without_cr(std::string_view value)
{
    if (!value.empty() && value.back() == '\r') {
        value.remove_suffix(1U);
    }
    return value;
}


struct StanzaBoundary {
    std::size_t end{0U};
    std::size_t next{0U};
};

StanzaBoundary next_stanza_boundary(
    const std::string_view content,
    const std::size_t start)
{
    const std::size_t lf = content.find("\n\n", start);
    const std::size_t crlf = content.find("\r\n\r\n", start);
    if (crlf != std::string_view::npos &&
        (lf == std::string_view::npos || crlf < lf)) {
        return {crlf, crlf + 4U};
    }
    if (lf != std::string_view::npos) {
        return {lf, lf + 2U};
    }
    return {content.size(), content.size()};
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
        if (line.rfind(field, 0U) == 0U) {
            value_start = start;
            line_end = end;
            return true;
        }
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1U;
    }
    return false;
}

bool is_deb822_binary_source(std::string_view block)
{
    std::size_t start = 0U;
    std::size_t end = 0U;
    if (!find_deb822_field(block, "URIs:", start, end) ||
        !find_deb822_field(block, "Suites:", start, end)) {
        return false;
    }

    if (!find_deb822_field(block, "Types:", start, end)) {
        return true;
    }

    std::string_view line =
        without_cr(block.substr(start, end - start));
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }

    std::string_view values = line.substr(colon + 1U);
    std::size_t word_start = 0U;
    while (word_start < values.size()) {
        while (word_start < values.size() &&
               std::isspace(
                   static_cast<unsigned char>(
                       values[word_start])) != 0) {
            ++word_start;
        }
        std::size_t word_end = word_start;
        while (word_end < values.size() &&
               std::isspace(
                   static_cast<unsigned char>(
                       values[word_end])) == 0) {
            ++word_end;
        }
        if (values.substr(
                word_start,
                word_end - word_start) == "deb") {
            return true;
        }
        word_start = word_end;
    }
    return false;
}

} // namespace

bool set_apt_list_entry_enabled(
    std::string_view content,
    const std::size_t line_number,
    const bool enabled,
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

    std::size_t start = 0U;
    std::size_t stanza = 0U;
    while (start < content.size()) {
        const StanzaBoundary boundary =
            next_stanza_boundary(content, start);
        const std::size_t end = boundary.end;
        ++stanza;

        if (stanza == stanza_number) {
            const std::string_view block =
                content.substr(start, end - start);
            if (!is_deb822_binary_source(block)) {
                error =
                    "Selected deb822 stanza is not an enabled-capable binary repository.";
                return false;
            }

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
                if (!replacement.empty() &&
                    replacement.back() != '\n') {
                    replacement.append(line_break);
                }
                replacement +=
                    enabled ? "Enabled: yes" : "Enabled: no";
            }

            updated.assign(content);
            updated.replace(
                start,
                end - start,
                replacement);
            return true;
        }

        start = boundary.next;
    }

    error = "APT source stanza no longer exists.";
    return false;
}

} // namespace infiltrator::software
