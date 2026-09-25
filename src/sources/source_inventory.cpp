// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/source_inventory.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {


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

std::string trim(std::string_view value)
{
    std::size_t first = 0U;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
        --last;
    }

    return std::string(value.substr(first, last - first));
}

std::vector<std::string> split_words(std::string_view value)
{
    std::istringstream input{std::string(value)};
    std::vector<std::string> words;
    std::string word;
    while (input >> word) {
        words.push_back(std::move(word));
    }
    return words;
}

std::string read_text_file(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input) {
        return {};
    }
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

void append_apt_file(
    std::vector<SourceRecord> &result,
    const std::filesystem::path &path)
{
    const std::string content = read_text_file(path);
    if (content.empty()) {
        return;
    }

    std::vector<SourceRecord> parsed;
    if (path.extension() == ".sources") {
        parsed = SourceInventory::parse_apt_deb822(
            content, path.string());
    } else {
        parsed = SourceInventory::parse_apt_list(
            content, path.string());
    }

    result.insert(
        result.end(),
        std::make_move_iterator(parsed.begin()),
        std::make_move_iterator(parsed.end()));
}

std::map<std::string, std::string> parse_key_values(
    std::string_view content)
{
    std::map<std::string, std::string> values;
    std::istringstream input{std::string(content)};
    std::string line;
    std::string current_key;

    while (std::getline(input, line)) {
        if (!line.empty() &&
            std::isspace(static_cast<unsigned char>(line.front())) != 0 &&
            !current_key.empty()) {
            values[current_key] += " " + trim(line);
            continue;
        }

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            current_key.clear();
            continue;
        }

        current_key = trim(
            std::string_view(line).substr(0U, colon));
        values[current_key] = trim(
            std::string_view(line).substr(colon + 1U));
    }

    return values;
}

bool parse_bool(std::string_view value, const bool fallback)
{
    std::string lower;
    lower.reserve(value.size());
    for (const char ch : value) {
        lower.push_back(
            static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lower == "yes" || lower == "true" || lower == "1") {
        return true;
    }
    if (lower == "no" || lower == "false" || lower == "0") {
        return false;
    }
    return fallback;
}

std::vector<SourceRecord> parse_flatpak_config(
    const std::filesystem::path &path,
    const std::string &scope)
{
    const std::string content = read_text_file(path);
    std::vector<SourceRecord> result;
    if (content.empty()) {
        return result;
    }

    std::istringstream input(content);
    std::string line;
    std::string remote_name;
    std::map<std::string, std::string> values;

    auto flush = [&]() {
        if (remote_name.empty()) {
            values.clear();
            return;
        }

        const auto url = values.find("url");
        if (url != values.end() && !url->second.empty()) {
            SourceRecord record;
            record.kind = SourceKind::flatpak;
            record.name = remote_name;
            record.location = url->second;
            record.scope = scope;
            record.backing_file = path.string();

            const auto title = values.find("xa.title");
            if (title != values.end()) {
                record.detail = title->second;
            }
            const auto disabled = values.find("xa.disable");
            if (disabled != values.end()) {
                record.enabled = !parse_bool(disabled->second, false);
            }

            result.emplace_back(std::move(record));
        }

        remote_name.clear();
        values.clear();
    };

    while (std::getline(input, line)) {
        const std::string clean = trim(line);
        if (clean.size() >= 11U &&
            clean.rfind("[remote \"", 0U) == 0U &&
            clean.back() == ']') {
            flush();
            const std::size_t first = clean.find('"');
            const std::size_t second =
                clean.find('"', first + 1U);
            if (first != std::string::npos &&
                second != std::string::npos &&
                second > first + 1U) {
                remote_name =
                    clean.substr(first + 1U, second - first - 1U);
            }
            continue;
        }

        if (remote_name.empty()) {
            continue;
        }

        const std::size_t equals = clean.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        values[trim(clean.substr(0U, equals))] =
            trim(clean.substr(equals + 1U));
    }

    flush();
    return result;
}

} // namespace

std::string_view source_kind_name(const SourceKind kind) noexcept
{
    switch (kind) {
    case SourceKind::infiltrator: return "Infiltrator";
    case SourceKind::apt: return "APT";
    case SourceKind::flatpak: return "Flatpak";
    }
    return "Unknown";
}

std::vector<SourceRecord> SourceInventory::parse_apt_list(
    const std::string_view content,
    const std::string_view backing_file)
{
    std::vector<SourceRecord> result;
    std::istringstream input{std::string(content)};
    std::string line;
    std::size_t line_number = 0U;

    while (std::getline(input, line)) {
        ++line_number;
        std::string clean = trim(line);
        bool enabled = true;

        if (!clean.empty() && clean.front() == '#') {
            clean = trim(std::string_view(clean).substr(1U));
            enabled = false;
        }

        if (clean.rfind("deb ", 0U) != 0U &&
            clean.rfind("deb-src ", 0U) != 0U) {
            continue;
        }

        std::vector<std::string> words = split_words(clean);
        if (words.size() < 3U) {
            continue;
        }

        std::size_t index = 1U;
        if (index < words.size() &&
            !words[index].empty() &&
            words[index].front() == '[') {
            while (index < words.size() &&
                   words[index].find(']') == std::string::npos) {
                ++index;
            }
            if (index < words.size()) {
                ++index;
            }
        }

        if (index + 1U >= words.size()) {
            continue;
        }

        SourceRecord record;
        record.kind = SourceKind::apt;
        record.name = words[index] + " " + words[index + 1U];
        record.location = words[index];
        record.scope = "System";
        record.backing_file = std::string(backing_file);
        record.entry_index = line_number;
        record.enabled = enabled;

        std::ostringstream detail;
        detail << words[index + 1U];
        for (std::size_t i = index + 2U; i < words.size(); ++i) {
            detail << ' ' << words[i];
        }
        record.detail = detail.str();
        result.emplace_back(std::move(record));
    }

    return result;
}

std::vector<SourceRecord> SourceInventory::parse_apt_deb822(
    const std::string_view content,
    const std::string_view backing_file)
{
    std::vector<SourceRecord> result;
    std::size_t start = 0U;
    std::size_t stanza_number = 0U;

    while (start < content.size()) {
        ++stanza_number;
        const StanzaBoundary boundary =
            next_stanza_boundary(content, start);
        const std::size_t end = boundary.end;

        const std::string_view block =
            content.substr(start, end - start);
        const auto values = parse_key_values(block);

        const auto uris = values.find("URIs");
        const auto suites = values.find("Suites");
        const auto types = values.find("Types");

        if (uris != values.end() &&
            suites != values.end() &&
            (types == values.end() ||
             types->second.find("deb") != std::string::npos)) {
            const bool enabled =
                values.find("Enabled") == values.end() ||
                parse_bool(values.at("Enabled"), true);

            for (const std::string &uri : split_words(uris->second)) {
                SourceRecord record;
                record.kind = SourceKind::apt;
                record.name = uri + " " + suites->second;
                record.location = uri;
                record.scope = "System";
                record.backing_file = std::string(backing_file);
                record.entry_index = stanza_number;
                record.enabled = enabled;

                record.detail = suites->second;
                const auto components = values.find("Components");
                if (components != values.end() &&
                    !components->second.empty()) {
                    record.detail += " · " + components->second;
                }

                result.emplace_back(std::move(record));
            }
        }

        start = boundary.next;
    }

    return result;
}

std::vector<SourceRecord> SourceInventory::list(std::string &error) const
{
    error.clear();
    std::vector<SourceRecord> result;

    SourceRecord infiltrator;
    infiltrator.kind = SourceKind::infiltrator;
    infiltrator.name = "Infiltrator Repository";
    infiltrator.location =
        "https://infiltrator-projects.github.io/Infiltrator-Repository/";
    infiltrator.detail = "Verified Infiltrator application catalogue";
    infiltrator.scope = "Project";
    infiltrator.enabled = true;
    result.emplace_back(std::move(infiltrator));

    const std::filesystem::path main_list{"/etc/apt/sources.list"};
    if (std::filesystem::exists(main_list)) {
        append_apt_file(result, main_list);
    }

    const std::filesystem::path source_dir{"/etc/apt/sources.list.d"};
    std::error_code ec;
    if (std::filesystem::is_directory(source_dir, ec)) {
        for (const auto &entry :
             std::filesystem::directory_iterator(source_dir, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const auto extension = entry.path().extension();
            if (extension == ".list" || extension == ".sources") {
                append_apt_file(result, entry.path());
            }
        }
    }

    const auto system_flatpak =
        parse_flatpak_config(
            "/var/lib/flatpak/repo/config", "System");
    result.insert(
        result.end(),
        system_flatpak.begin(),
        system_flatpak.end());

    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        const std::filesystem::path user_config =
            std::filesystem::path(home) /
            ".local/share/flatpak/repo/config";
        const auto user_flatpak =
            parse_flatpak_config(user_config, "User");
        result.insert(
            result.end(),
            user_flatpak.begin(),
            user_flatpak.end());
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const SourceRecord &left, const SourceRecord &right) {
            if (left.kind != right.kind) {
                return static_cast<int>(left.kind) <
                       static_cast<int>(right.kind);
            }
            return left.name < right.name;
        });

    return result;
}

} // namespace infiltrator::software
