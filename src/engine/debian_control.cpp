// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_control.hpp"

#include "core/text.hpp"

namespace infiltrator::software {

bool parse_debian_control_records(
    const std::string_view content,
    const DebianControlRecordCallback &record,
    std::string &error)
{
    error.clear();
    DebianControlFields fields;
    std::string current_key;

    const auto flush = [&]() {
        if (!fields.empty()) {
            record(fields);
        }
        fields.clear();
        current_key.clear();
    };

    std::size_t start = 0U;
    while (start <= content.size()) {
        const std::size_t newline = content.find('\n', start);
        const std::size_t end =
            newline == std::string_view::npos ? content.size() : newline;

        std::string_view line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }

        if (line.find_first_not_of(" \t") == std::string_view::npos) {
            flush();
        } else if (line.front() == ' ' || line.front() == '\t') {
            if (current_key.empty()) {
                error =
                    "Debian control continuation line has no preceding field.";
                return false;
            }
            if (!fields[current_key].empty()) {
                fields[current_key].push_back('\n');
            }
            fields[current_key] += text::trim(line);
        } else {
            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                error =
                    "Malformed Debian control line without a field separator.";
                return false;
            }

            current_key = text::lower_ascii(
                text::trim(line.substr(0U, colon)));
            if (current_key.empty()) {
                error = "Debian control field name is empty.";
                return false;
            }

            const auto inserted = fields.emplace(
                current_key,
                text::trim(line.substr(colon + 1U)));
            if (!inserted.second) {
                error = "Duplicate Debian control field: " + current_key;
                return false;
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1U;
    }

    flush();
    return true;
}

} // namespace infiltrator::software
