// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_preferences.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <fnmatch.h>
#include <iterator>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

std::string trim(const std::string_view value)
{
    std::size_t first = 0U;
    while (first < value.size() &&
           std::isspace(
               static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first &&
           std::isspace(
               static_cast<unsigned char>(value[last - 1U])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string lower_ascii(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

std::vector<std::string> split_words(const std::string_view value)
{
    std::istringstream input{std::string(value)};
    std::vector<std::string> result;
    std::string word;
    while (input >> word) {
        result.emplace_back(std::move(word));
    }
    return result;
}

bool glob_match(
    const std::string_view pattern,
    const std::string_view value)
{
    return fnmatch(
               std::string(pattern).c_str(),
               std::string(value).c_str(),
               0) == 0;
}

bool regex_match_pattern(
    const std::string_view pattern,
    const std::string_view value)
{
    if (pattern.size() < 2U ||
        pattern.front() != '/' ||
        pattern.back() != '/') {
        return false;
    }

    try {
        const std::regex expression(
            std::string(pattern.substr(
                1U, pattern.size() - 2U)),
            std::regex::extended);
        return std::regex_match(
            value.begin(), value.end(), expression);
    } catch (const std::regex_error &) {
        return false;
    }
}

bool pattern_match(
    const std::string_view pattern,
    const std::string_view value)
{
    if (pattern == "*") {
        return true;
    }
    if (pattern.size() >= 2U &&
        pattern.front() == '/' &&
        pattern.back() == '/') {
        return regex_match_pattern(pattern, value);
    }
    return glob_match(pattern, value);
}

bool package_pattern_matches(
    std::string_view pattern,
    const DebianPackageVersion &package)
{
    if (pattern.rfind("src:", 0U) == 0U) {
        return false;
    }

    std::string_view architecture;
    const std::size_t colon = pattern.rfind(':');
    if (colon != std::string_view::npos &&
        colon + 1U < pattern.size()) {
        architecture = pattern.substr(colon + 1U);
        pattern = pattern.substr(0U, colon);
    }

    if (!architecture.empty() &&
        architecture != "any" &&
        architecture != package.architecture) {
        return false;
    }

    return pattern_match(pattern, package.package);
}

bool package_rule_matches(
    const DebianPreferenceRule &rule,
    const DebianPackageVersion &package)
{
    for (const std::string &pattern : rule.packages) {
        if (package_pattern_matches(pattern, package)) {
            return true;
        }
    }
    return false;
}

std::string release_value(
    const DebianPackageVersion &package,
    const char key)
{
    switch (key) {
        case 'a':
            return package.release_archive;
        case 'n':
            return package.release_codename;
        case 'o':
            return package.release_origin;
        case 'l':
            return package.release_label;
        case 'c':
            return package.component;
        case 'v':
            return package.release_version;
        case 'b':
            return package.architecture;
        default:
            return {};
    }
}

bool pin_rule_matches(
    const DebianPreferenceRule &rule,
    const DebianPackageVersion &package)
{
    switch (rule.kind) {
        case DebianPinKind::any:
            return true;
        case DebianPinKind::origin:
            return pattern_match(rule.pattern, package.site);
        case DebianPinKind::version:
            return pattern_match(rule.pattern, package.version);
        case DebianPinKind::release:
            if (rule.release_conditions.empty()) {
                return rule.pattern.empty() ||
                       rule.pattern == "*" ||
                       pattern_match(
                           rule.pattern,
                           package.release_archive);
            }
            for (const auto &[key, pattern] :
                 rule.release_conditions) {
                if (!pattern_match(
                        pattern,
                        release_value(package, key))) {
                    return false;
                }
            }
            return true;
    }
    return false;
}

using Fields = std::map<std::string, std::string>;

Fields parse_fields(const std::string_view block)
{
    Fields result;
    std::istringstream input{std::string(block)};
    std::string line;
    std::string current;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }

        if ((line.front() == ' ' || line.front() == '\t') &&
            !current.empty()) {
            const std::string continued = trim(line);
            if (!continued.empty()) {
                if (!result[current].empty()) {
                    result[current].push_back(' ');
                }
                result[current] += continued;
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

bool parse_priority(
    const std::string_view text,
    int &priority)
{
    priority = 0;
    const std::string clean = trim(text);
    if (clean.empty()) {
        return false;
    }

    long long parsed = 0;
    const auto conversion =
        std::from_chars(
            clean.data(),
            clean.data() + clean.size(),
            parsed);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != clean.data() + clean.size() ||
        parsed == 0 ||
        parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
        return false;
    }

    priority = static_cast<int>(parsed);
    return true;
}

bool parse_release_pin(
    const std::string_view value,
    DebianPreferenceRule &rule)
{
    const std::string clean = trim(value);
    if (clean.empty() || clean == "*") {
        rule.pattern = "*";
        return true;
    }

    if (clean.find('=') == std::string::npos) {
        rule.pattern = clean;
        return true;
    }

    std::size_t start = 0U;
    while (start <= clean.size()) {
        const std::size_t comma = clean.find(',', start);
        const std::size_t end =
            comma == std::string::npos
                ? clean.size()
                : comma;
        const std::string item =
            trim(std::string_view(clean).substr(
                start, end - start));
        if (!item.empty()) {
            const std::size_t equals = item.find('=');
            if (equals == std::string::npos ||
                equals == 0U ||
                equals + 1U >= item.size()) {
                return false;
            }
            const std::string key =
                lower_ascii(trim(
                    std::string_view(item).substr(
                        0U, equals)));
            const std::string pattern =
                trim(std::string_view(item).substr(
                    equals + 1U));
            if (key.size() != 1U ||
                std::string("anocl vb").find(key[0]) ==
                    std::string::npos ||
                pattern.empty()) {
                return false;
            }
            rule.release_conditions.emplace_back(
                key[0], pattern);
        }

        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1U;
    }

    return !rule.release_conditions.empty();
}

bool parse_pin(
    const std::string_view text,
    DebianPreferenceRule &rule)
{
    const std::string clean = trim(text);
    if (clean.empty() || clean == "*") {
        rule.kind = DebianPinKind::any;
        rule.pattern = "*";
        return true;
    }

    const std::size_t space = clean.find_first_of(" \t");
    const std::string kind =
        lower_ascii(
            space == std::string::npos
                ? clean
                : clean.substr(0U, space));
    const std::string value =
        space == std::string::npos
            ? std::string{}
            : trim(std::string_view(clean).substr(space + 1U));

    if (kind == "origin") {
        rule.kind = DebianPinKind::origin;
        if (value == """" || value == "''") {
            rule.pattern.clear();
        } else {
            rule.pattern = value;
        }
        return true;
    }
    if (kind == "version") {
        rule.kind = DebianPinKind::version;
        rule.pattern = value.empty() ? "*" : value;
        return true;
    }
    if (kind == "release") {
        rule.kind = DebianPinKind::release;
        return parse_release_pin(value, rule);
    }

    return false;
}

bool has_suffix(
    const std::string_view value,
    const std::string_view suffix)
{
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

bool preference_filename(
    const std::filesystem::path &path)
{
    const std::string name = path.filename().string();
    if (name.empty() || name.front() == '.') {
        return false;
    }
    if (name.back() == '~' ||
        has_suffix(name, ".bak") ||
        has_suffix(name, ".disabled") ||
        has_suffix(name, ".save") ||
        has_suffix(name, ".distUpgrade")) {
        return false;
    }

    for (const unsigned char ch : name) {
        if (std::isalnum(ch) != 0 ||
            ch == '-' || ch == '_' || ch == '.') {
            continue;
        }
        return false;
    }
    return true;
}

bool read_text(
    const std::filesystem::path &path,
    std::string &content,
    std::string &error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error =
            "Unable to read APT preferences file: " +
            path.string();
        return false;
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error =
            "Unable to finish reading APT preferences file: " +
            path.string();
        return false;
    }

    content = stream.str();
    return true;
}

} // namespace

DebianAptPreferences DebianAptPreferences::parse(
    const std::string_view content,
    const std::string_view origin,
    std::string &error)
{
    DebianAptPreferences result;
    error.clear();

    std::size_t start = 0U;
    std::size_t record_number = 0U;
    while (start < content.size()) {
        std::size_t end = content.find("\n\n", start);
        if (end == std::string_view::npos) {
            end = content.size();
        }
        const std::string_view block =
            content.substr(start, end - start);
        start =
            end == content.size()
                ? content.size()
                : end + 2U;
        ++record_number;

        const Fields fields = parse_fields(block);
        if (fields.empty()) {
            continue;
        }

        const auto packages = fields.find("package");
        const auto pin = fields.find("pin");
        const auto priority = fields.find("pin-priority");
        if (packages == fields.end() ||
            pin == fields.end() ||
            priority == fields.end()) {
            continue;
        }

        DebianPreferenceRule rule;
        rule.packages = split_words(packages->second);
        if (rule.packages.empty()) {
            continue;
        }

        if (!parse_priority(
                priority->second,
                rule.priority)) {
            error =
                std::string(origin) +
                ": preference record " +
                std::to_string(record_number) +
                " has an invalid Pin-Priority.";
            return {};
        }

        if (!parse_pin(pin->second, rule)) {
            error =
                std::string(origin) +
                ": preference record " +
                std::to_string(record_number) +
                " has an unsupported Pin expression.";
            return {};
        }

        rule.generic =
            rule.packages.size() == 1U &&
            rule.packages.front() == "*" &&
            rule.kind != DebianPinKind::version;

        result.rules_.emplace_back(std::move(rule));
    }

    return result;
}

DebianAptPreferences DebianAptPreferences::read(
    std::string &error)
{
    DebianAptPreferences result;
    error.clear();

    std::vector<std::filesystem::path> files;
    const std::filesystem::path main{"/etc/apt/preferences"};
    std::error_code ec;
    if (std::filesystem::is_regular_file(main, ec) && !ec) {
        files.emplace_back(main);
    }
    ec.clear();

    const std::filesystem::path directory{
        "/etc/apt/preferences.d"};
    if (std::filesystem::is_directory(directory, ec) && !ec) {
        for (const auto &entry :
             std::filesystem::directory_iterator(
                 directory, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            if (preference_filename(entry.path())) {
                files.emplace_back(entry.path());
            }
        }
    }
    if (ec) {
        error =
            "Unable to enumerate APT preferences: " +
            ec.message();
        return {};
    }

    std::sort(files.begin(), files.end());
    if (const auto found =
            std::find(files.begin(), files.end(), main);
        found != files.end() &&
        found != files.begin()) {
        std::rotate(files.begin(), found, found + 1U);
    }

    for (const std::filesystem::path &path : files) {
        std::string content;
        if (!read_text(path, content, error)) {
            return {};
        }

        DebianAptPreferences parsed =
            parse(content, path.string(), error);
        if (!error.empty()) {
            return {};
        }
        result.append(std::move(parsed));
    }

    return result;
}

void DebianAptPreferences::append(
    DebianAptPreferences other)
{
    rules_.insert(
        rules_.end(),
        std::make_move_iterator(other.rules_.begin()),
        std::make_move_iterator(other.rules_.end()));
}

int DebianAptPreferences::priority_for(
    const DebianPackageVersion &package) const
{
    for (const DebianPreferenceRule &rule : rules_) {
        if (!rule.generic &&
            package_rule_matches(rule, package) &&
            pin_rule_matches(rule, package)) {
            return rule.priority;
        }
    }

    bool matched_generic = false;
    int generic_priority =
        std::numeric_limits<int>::min();
    for (const DebianPreferenceRule &rule : rules_) {
        if (!rule.generic ||
            !package_rule_matches(rule, package) ||
            !pin_rule_matches(rule, package)) {
            continue;
        }
        matched_generic = true;
        generic_priority =
            std::max(generic_priority, rule.priority);
    }

    return matched_generic
        ? generic_priority
        : package.pin_priority;
}

} // namespace infiltrator::software
