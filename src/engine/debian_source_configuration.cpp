// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_source_configuration.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

std::string trim(const std::string_view value)
{
    std::size_t first = 0U;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0) ++first;
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) --last;
    return std::string(value.substr(first, last - first));
}

std::string lower_ascii(std::string value)
{
    for (char &character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::vector<std::string> split_words(const std::string_view value)
{
    std::istringstream input{std::string(value)};
    std::vector<std::string> result;
    std::string word;
    while (input >> word) result.emplace_back(std::move(word));
    return result;
}

std::vector<std::string> split_commas(const std::string_view value)
{
    std::vector<std::string> result;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end =
            comma == std::string_view::npos ? value.size() : comma;
        const std::string item = trim(value.substr(start, end - start));
        if (!item.empty()) result.emplace_back(item);
        if (comma == std::string_view::npos) break;
        start = comma + 1U;
    }
    return result;
}

bool parse_yes_no(const std::string_view value, const bool fallback)
{
    const std::string lower = lower_ascii(trim(value));
    if (lower == "yes" || lower == "true" || lower == "1") return true;
    if (lower == "no" || lower == "false" || lower == "0") return false;
    return fallback;
}

bool parse_yes_no_strict(
    const std::string_view value,
    bool &parsed)
{
    const std::string lower = lower_ascii(trim(value));
    if (lower == "yes" || lower == "true" || lower == "1") {
        parsed = true;
        return true;
    }
    if (lower == "no" || lower == "false" || lower == "0") {
        parsed = false;
        return true;
    }
    return false;
}

bool parse_seconds(
    const std::string_view value,
    std::uint64_t &parsed)
{
    const std::string clean = trim(value);
    if (clean.empty()) return false;
    std::uint64_t result = 0U;
    const auto converted = std::from_chars(
        clean.data(), clean.data() + clean.size(), result);
    if (converted.ec != std::errc{} ||
        converted.ptr != clean.data() + clean.size()) {
        return false;
    }
    parsed = result;
    return true;
}

std::string normalise_line_endings(const std::string_view content)
{
    std::string result;
    result.reserve(content.size());
    for (std::size_t index = 0U; index < content.size(); ++index) {
        if (content[index] == '\r') {
            if (index + 1U < content.size() &&
                content[index + 1U] == '\n') {
                continue;
            }
            result.push_back('\n');
            continue;
        }
        result.push_back(content[index]);
    }
    return result;
}

bool valid_fingerprint_selector(
    std::string_view value)
{
    if (!value.empty() && value.back() == '!') {
        value.remove_suffix(1U);
    }
    if (value.size() != 40U && value.size() != 64U) {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [](const unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

void append_signed_by(
    DebianRepositorySource &source,
    const std::string_view value,
    std::string &error)
{
    const std::string clean = trim(value);
    if (clean.empty()) return;

    if (clean.find("-----BEGIN PGP PUBLIC KEY BLOCK-----") !=
        std::string::npos) {
        if (clean.find("-----END PGP PUBLIC KEY BLOCK-----") ==
            std::string::npos) {
            error =
                "Inline Signed-By public key is incomplete.";
            return;
        }
        source.inline_keys.emplace_back(clean);
        return;
    }

    bool saw_value = false;
    for (const std::string &word : split_words(clean)) {
        for (const std::string &value_part : split_commas(word)) {
            if (value_part.empty()) continue;
            saw_value = true;
            if (value_part.front() == '/') {
                source.keyrings.emplace_back(value_part);
            } else if (valid_fingerprint_selector(value_part)) {
                source.allowed_fingerprints.emplace_back(value_part);
            } else {
                error =
                    "Signed-By contains an unsupported keyring or fingerprint selector.";
                source.keyrings.clear();
                source.allowed_fingerprints.clear();
                return;
            }
        }
    }
    if (!saw_value) {
        error = "Signed-By contains no usable keyring or fingerprint selector.";
    }
}

void parse_list_options(
    const std::string_view options,
    DebianRepositorySource &source,
    std::string &error)
{
    for (const std::string &option : split_words(options)) {
        const std::size_t equals = option.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = lower_ascii(option.substr(0U, equals));
        const std::string value = option.substr(equals + 1U);
        if (key == "signed-by") {
            append_signed_by(source, value, error);
            if (!error.empty()) return;
        } else if (key == "arch") {
            source.architectures = split_commas(value);
        } else if (key == "arch+") {
            source.architecture_additions = split_commas(value);
        } else if (key == "arch-") {
            source.architecture_removals = split_commas(value);
        } else if (key == "trusted") {
            source.verify_signatures = !parse_yes_no(value, false);
        } else if (key == "check-valid-until") {
            if (!parse_yes_no_strict(value, source.check_valid_until)) {
                error = "Check-Valid-Until must be yes or no.";
                return;
            }
        } else if (key == "check-date") {
            if (!parse_yes_no_strict(value, source.check_date)) {
                error = "Check-Date must be yes or no.";
                return;
            }
        } else if (key == "valid-until-min") {
            if (!parse_seconds(value, source.valid_until_min_seconds)) {
                error = "Valid-Until-Min must be a non-negative number of seconds.";
                return;
            }
        } else if (key == "valid-until-max") {
            if (!parse_seconds(value, source.valid_until_max_seconds)) {
                error = "Valid-Until-Max must be a non-negative number of seconds.";
                return;
            }
        } else if (key == "date-max-future") {
            if (!parse_seconds(value, source.date_max_future_seconds)) {
                error = "Date-Max-Future must be a non-negative number of seconds.";
                return;
            }
        }
    }
}

using Fields = std::map<std::string, std::string>;

Fields parse_fields(const std::string_view block)
{
    Fields result;
    std::istringstream input{std::string(block)};
    std::string line;
    std::string current;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() &&
            std::isspace(static_cast<unsigned char>(line.front())) != 0 &&
            !current.empty()) {
            const std::string continuation = trim(line);
            result[current] += "\n";
            if (continuation != ".") {
                result[current] += continuation;
            }
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            current.clear();
            continue;
        }
        current = lower_ascii(trim(
            std::string_view(line).substr(0U, colon)));
        result[current] =
            trim(std::string_view(line).substr(colon + 1U));
    }
    return result;
}

bool contains_word(
    const std::string_view words,
    const std::string_view expected)
{
    for (const std::string &word : split_words(words)) {
        if (word == expected) return true;
    }
    return false;
}

bool read_text(
    const std::filesystem::path &path,
    std::string &content,
    std::string &error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Unable to read repository source file: " + path.string();
        return false;
    }
    std::ostringstream text;
    text << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error =
            "Unable to finish reading repository source file: " +
            path.string();
        return false;
    }
    content = text.str();
    return true;
}

} // namespace

std::vector<DebianRepositorySource>
DebianSourceConfiguration::parse_list(
    const std::string_view content,
    const std::string_view origin,
    std::string &error)
{
    error.clear();
    std::vector<DebianRepositorySource> result;
    std::istringstream input{std::string(content)};
    std::string line;
    std::size_t line_number = 0U;

    while (std::getline(input, line)) {
        ++line_number;
        std::string clean = trim(line);
        if (clean.empty() || clean.front() == '#') continue;
        const std::size_t comment = clean.find('#');
        if (comment != std::string::npos) {
            clean = trim(std::string_view(clean).substr(0U, comment));
        }
        if (clean.rfind("deb-src ", 0U) == 0U) continue;
        if (clean.rfind("deb ", 0U) != 0U) continue;

        clean = trim(std::string_view(clean).substr(4U));
        std::string options;
        if (!clean.empty() && clean.front() == '[') {
            const std::size_t close = clean.find(']');
            if (close == std::string::npos) {
                error = std::string(origin) + ":" +
                    std::to_string(line_number) +
                    ": repository option block is unterminated.";
                return {};
            }
            options = clean.substr(1U, close - 1U);
            clean = trim(std::string_view(clean).substr(close + 1U));
        }

        const std::vector<std::string> words = split_words(clean);
        if (words.size() < 2U) {
            error = std::string(origin) + ":" +
                std::to_string(line_number) +
                ": Debian repository URI and suite are required.";
            return {};
        }

        DebianRepositorySource source;
        source.id = std::string(origin) + ":" + std::to_string(line_number);
        source.uri = words[0];
        source.suite = words[1];
        for (std::size_t index = 2U; index < words.size(); ++index) {
            source.components.emplace_back(words[index]);
        }
        parse_list_options(options, source, error);
        if (!error.empty()) {
            error = std::string(origin) + ":" +
                std::to_string(line_number) + ": " + error;
            return {};
        }
        result.emplace_back(std::move(source));
    }
    return result;
}

std::vector<DebianRepositorySource>
DebianSourceConfiguration::parse_deb822(
    const std::string_view content,
    const std::string_view origin,
    std::string &error)
{
    error.clear();
    std::vector<DebianRepositorySource> result;
    const std::string normalized = normalise_line_endings(content);
    const std::string_view normalized_view(normalized);
    std::size_t start = 0U;
    std::size_t stanza = 0U;

    while (start < normalized_view.size()) {
        std::size_t end = normalized_view.find("\n\n", start);
        if (end == std::string_view::npos) end = normalized_view.size();
        const std::string_view block =
            normalized_view.substr(start, end - start);
        start =
            end == normalized_view.size()
                ? normalized_view.size()
                : end + 2U;
        ++stanza;

        const Fields fields = parse_fields(block);
        if (fields.empty()) continue;
        const auto types = fields.find("types");
        if (types != fields.end() && !contains_word(types->second, "deb")) continue;
        const auto enabled = fields.find("enabled");
        if (enabled != fields.end() && !parse_yes_no(enabled->second, true)) continue;

        const auto uris = fields.find("uris");
        const auto suites = fields.find("suites");
        if (uris == fields.end() || suites == fields.end()) continue;

        const std::vector<std::string> uri_values = split_words(uris->second);
        const std::vector<std::string> suite_values = split_words(suites->second);
        if (uri_values.empty() || suite_values.empty()) {
            error = std::string(origin) + ": stanza " +
                std::to_string(stanza) +
                ": Debian repository URI and suite are required.";
            return {};
        }

        std::vector<std::string> components;
        if (const auto found = fields.find("components"); found != fields.end()) {
            components = split_words(found->second);
        }
        std::vector<std::string> architectures;
        if (const auto found = fields.find("architectures"); found != fields.end()) {
            architectures = split_words(found->second);
        }
        std::vector<std::string> architecture_additions;
        if (const auto found = fields.find("architectures-add");
            found != fields.end()) {
            architecture_additions = split_words(found->second);
        }
        std::vector<std::string> architecture_removals;
        if (const auto found = fields.find("architectures-remove");
            found != fields.end()) {
            architecture_removals = split_words(found->second);
        }

        bool check_valid_until = true;
        bool check_date = true;
        std::uint64_t valid_until_min_seconds = 0U;
        std::uint64_t valid_until_max_seconds = 0U;
        std::uint64_t date_max_future_seconds = 10U;

        const auto parse_boolean_field =
            [&](const char *name, bool &destination) -> bool {
                const auto found = fields.find(name);
                if (found == fields.end()) return true;
                if (parse_yes_no_strict(found->second, destination)) return true;
                error = std::string(origin) + ": stanza " +
                    std::to_string(stanza) + ": " + name +
                    " must be yes or no.";
                return false;
            };
        const auto parse_seconds_field =
            [&](const char *name, std::uint64_t &destination) -> bool {
                const auto found = fields.find(name);
                if (found == fields.end()) return true;
                if (parse_seconds(found->second, destination)) return true;
                error = std::string(origin) + ": stanza " +
                    std::to_string(stanza) + ": " + name +
                    " must be a non-negative number of seconds.";
                return false;
            };

        if (!parse_boolean_field("check-valid-until", check_valid_until) ||
            !parse_boolean_field("check-date", check_date) ||
            !parse_seconds_field(
                "valid-until-min", valid_until_min_seconds) ||
            !parse_seconds_field(
                "valid-until-max", valid_until_max_seconds) ||
            !parse_seconds_field(
                "date-max-future", date_max_future_seconds)) {
            return {};
        }

        std::string signed_by;
        if (const auto found = fields.find("signed-by"); found != fields.end()) {
            signed_by = found->second;
        }
        bool verify_signatures = true;
        if (const auto found = fields.find("trusted"); found != fields.end()) {
            verify_signatures = !parse_yes_no(found->second, false);
        }

        std::size_t ordinal = 0U;
        for (const std::string &uri : uri_values) {
            for (const std::string &suite : suite_values) {
                DebianRepositorySource source;
                source.id = std::string(origin) + ":stanza-" +
                    std::to_string(stanza) + ":" + std::to_string(++ordinal);
                source.uri = uri;
                source.suite = suite;
                source.components = components;
                source.architectures = architectures;
                source.architecture_additions = architecture_additions;
                source.architecture_removals = architecture_removals;
                source.verify_signatures = verify_signatures;
                source.check_valid_until = check_valid_until;
                source.check_date = check_date;
                source.valid_until_min_seconds =
                    valid_until_min_seconds;
                source.valid_until_max_seconds =
                    valid_until_max_seconds;
                source.date_max_future_seconds =
                    date_max_future_seconds;
                append_signed_by(source, signed_by, error);
                if (!error.empty()) {
                    error = std::string(origin) + ": stanza " +
                        std::to_string(stanza) + ": " + error;
                    return {};
                }
                result.emplace_back(std::move(source));
            }
        }
    }
    return result;
}

std::vector<DebianRepositorySource>
DebianSourceConfiguration::read(std::string &error)
{
    error.clear();
    std::vector<DebianRepositorySource> result;
    std::vector<std::filesystem::path> files;

    /*
     * Release-upgrade planning must be able to reconcile the next Mint
     * release against its supplied repository list without changing the
     * running system first. The override is explicit and process-local.
     */
    const char *override_file =
        std::getenv("INFILTRATOR_SOFTWARE_SOURCES_FILE");
    const char *override_origin =
        std::getenv("INFILTRATOR_SOFTWARE_SOURCES_ORIGIN");
    if (override_file != nullptr &&
        *override_file != '\0') {
        const std::filesystem::path path{
            override_file};
        std::error_code override_error;
        if (!std::filesystem::is_regular_file(
                path, override_error) ||
            override_error) {
            error =
                "The alternate repository source file is unavailable: " +
                path.string();
            return {};
        }
        files.emplace_back(path);
    }

    const std::filesystem::path main{"/etc/apt/sources.list"};
    std::error_code ec;
    if (files.empty()) {
        if (std::filesystem::is_regular_file(main, ec) && !ec) {
            files.emplace_back(main);
        }
        ec.clear();

        const std::filesystem::path directory{"/etc/apt/sources.list.d"};
        if (std::filesystem::is_directory(directory, ec) && !ec) {
        for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            const auto extension = entry.path().extension();
            if (extension == ".list" || extension == ".sources") files.emplace_back(entry.path());
        }
        }
    }
    if (ec) {
        error = "Unable to enumerate Debian repository source files: " + ec.message();
        return {};
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path &path : files) {
        std::string content;
        if (!read_text(path, content, error)) return {};
        const std::string origin =
            override_file != nullptr &&
            *override_file != '\0' &&
            path == std::filesystem::path(override_file) &&
            override_origin != nullptr &&
            *override_origin != '\0'
                ? std::string(override_origin)
                : path.string();
        std::vector<DebianRepositorySource> parsed =
            path.extension() == ".sources"
                ? parse_deb822(content, origin, error)
                : parse_list(content, origin, error);
        if (!error.empty()) return {};
        result.insert(
            result.end(),
            std::make_move_iterator(parsed.begin()),
            std::make_move_iterator(parsed.end()));
    }

    if (result.empty()) error = "No enabled Debian binary repositories are configured.";
    return result;
}

} // namespace infiltrator::software
