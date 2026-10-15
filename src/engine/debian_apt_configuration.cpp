// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_apt_configuration.hpp"

#include "core/text.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

using text::lower_ascii;

bool valid_name_character(const unsigned char ch)
{
    return std::isalnum(ch) != 0 ||
        ch == '/' || ch == '-' || ch == ':' || ch == '.' ||
        ch == '_' || ch == '+';
}

struct ParseState {
    std::map<std::string, std::string> values;
    std::vector<std::filesystem::path> include_stack;
};

std::string scoped_key(
    const std::vector<std::string> &scope,
    const std::string_view name)
{
    std::string result;
    for (const std::string &part : scope) {
        if (!result.empty()) {
            result += "::";
        }
        result += part;
    }
    if (!name.empty()) {
        if (!result.empty()) {
            result += "::";
        }
        result += name;
    }
    return lower_ascii(std::move(result));
}

void clear_prefix(
    std::map<std::string, std::string> &values,
    const std::string &prefix)
{
    for (auto iterator = values.begin(); iterator != values.end();) {
        const bool exact = iterator->first == prefix;
        const bool child =
            iterator->first.size() > prefix.size() + 1U &&
            iterator->first.compare(0U, prefix.size(), prefix) == 0 &&
            iterator->first.compare(prefix.size(), 2U, "::") == 0;
        if (exact || child) {
            iterator = values.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

class Parser final {
public:
    Parser(
        std::string_view content,
        std::string origin,
        ParseState &state,
        const std::filesystem::path &include_base)
        : content_(content),
          origin_(std::move(origin)),
          state_(state),
          include_base_(include_base)
    {
    }

    bool parse(std::string &error)
    {
        std::vector<std::string> scope;
        if (!parse_statements(scope, false, error)) {
            return false;
        }
        skip_space_and_comments();
        if (position_ != content_.size()) {
            return fail(
                "unexpected trailing configuration text",
                error);
        }
        return true;
    }

    static bool read_and_parse_file(
        const std::filesystem::path &path,
        ParseState &state,
        std::string &error);

private:
    void skip_space_and_comments()
    {
        for (;;) {
            while (position_ < content_.size() &&
                   std::isspace(
                       static_cast<unsigned char>(
                           content_[position_])) != 0) {
                ++position_;
            }
            if (position_ + 1U < content_.size() &&
                content_[position_] == '/' &&
                content_[position_ + 1U] == '/') {
                position_ += 2U;
                while (position_ < content_.size() &&
                       content_[position_] != '\n') {
                    ++position_;
                }
                continue;
            }
            if (position_ + 1U < content_.size() &&
                content_[position_] == '/' &&
                content_[position_ + 1U] == '*') {
                position_ += 2U;
                const std::size_t end =
                    content_.find("*/", position_);
                position_ =
                    end == std::string_view::npos
                        ? content_.size()
                        : end + 2U;
                continue;
            }
            if (position_ < content_.size() &&
                content_[position_] == '#') {
                if (directive_here("#include") ||
                    directive_here("#clear")) {
                    return;
                }
                while (position_ < content_.size() &&
                       content_[position_] != '\n') {
                    ++position_;
                }
                continue;
            }
            return;
        }
    }

    bool directive_here(
        const std::string_view directive) const
    {
        if (position_ + directive.size() > content_.size() ||
            content_.substr(position_, directive.size()) !=
                directive) {
            return false;
        }
        const std::size_t after =
            position_ + directive.size();
        return after == content_.size() ||
            std::isspace(
                static_cast<unsigned char>(content_[after])) != 0;
    }

    bool parse_statements(
        std::vector<std::string> &scope,
        const bool until_close,
        std::string &error)
    {
        for (;;) {
            skip_space_and_comments();
            if (position_ >= content_.size()) {
                if (until_close) {
                    return fail(
                        "unterminated configuration scope",
                        error);
                }
                return true;
            }
            if (content_[position_] == '}') {
                if (!until_close) {
                    return fail(
                        "unexpected closing brace",
                        error);
                }
                ++position_;
                return true;
            }
            if (directive_here("#include")) {
                if (!parse_include(error)) {
                    return false;
                }
                continue;
            }
            if (directive_here("#clear")) {
                if (!parse_clear(scope, error)) {
                    return false;
                }
                continue;
            }
            if (!parse_statement(scope, error)) {
                return false;
            }
        }
    }

    bool parse_statement(
        std::vector<std::string> &scope,
        std::string &error)
    {
        skip_space_and_comments();
        if (position_ >= content_.size()) {
            return true;
        }

        if (content_[position_] == '"') {
            std::string ignored;
            return parse_quoted(ignored, error) &&
                   expect(';', error);
        }

        std::string name;
        if (!parse_name(name, error)) {
            return false;
        }
        skip_space_and_comments();
        if (position_ >= content_.size()) {
            return fail(
                "missing value or scope after option name",
                error);
        }

        if (content_[position_] == '{') {
            ++position_;
            scope.push_back(name);
            const bool ok =
                parse_statements(scope, true, error);
            scope.pop_back();
            return ok && expect(';', error);
        }

        std::string value;
        if (!parse_quoted(value, error) ||
            !expect(';', error)) {
            return false;
        }
        state_.values[scoped_key(scope, name)] =
            std::move(value);
        return true;
    }

    bool parse_name(
        std::string &name,
        std::string &error)
    {
        skip_space_and_comments();
        const std::size_t start = position_;
        while (position_ < content_.size() &&
               valid_name_character(
                   static_cast<unsigned char>(
                       content_[position_]))) {
            ++position_;
        }
        if (position_ == start) {
            return fail(
                "expected an APT configuration option name",
                error);
        }
        name.assign(
            content_.substr(start, position_ - start));
        return true;
    }

    bool parse_quoted(
        std::string &value,
        std::string &error)
    {
        skip_space_and_comments();
        if (position_ >= content_.size() ||
            content_[position_] != '"') {
            return fail(
                "expected a quoted APT configuration value",
                error);
        }
        ++position_;
        const std::size_t start = position_;
        while (position_ < content_.size() &&
               content_[position_] != '"') {
            if (content_[position_] == '\\' ||
                content_[position_] == '\n' ||
                content_[position_] == '\r') {
                return fail(
                    "invalid character in quoted APT configuration value",
                    error);
            }
            ++position_;
        }
        if (position_ >= content_.size()) {
            return fail(
                "unterminated quoted APT configuration value",
                error);
        }
        value.assign(
            content_.substr(start, position_ - start));
        ++position_;
        return true;
    }

    bool expect(
        const char token,
        std::string &error)
    {
        skip_space_and_comments();
        if (position_ >= content_.size() ||
            content_[position_] != token) {
            return fail(
                std::string("expected '") + token + "'",
                error);
        }
        ++position_;
        return true;
    }

    bool parse_clear(
        const std::vector<std::string> &scope,
        std::string &error)
    {
        position_ += std::string_view("#clear").size();
        std::string name;
        if (!parse_name(name, error) ||
            !expect(';', error)) {
            return false;
        }
        clear_prefix(
            state_.values,
            scoped_key(scope, name));
        return true;
    }

    bool parse_include(std::string &error)
    {
        position_ += std::string_view("#include").size();
        std::string include;
        if (!parse_quoted(include, error) ||
            !expect(';', error)) {
            return false;
        }
        if (include_base_.empty()) {
            return fail(
                "#include is unavailable when parsing an in-memory fixture",
                error);
        }

        std::filesystem::path path(include);
        if (path.is_relative()) {
            path = include_base_ / path;
        }
        return parse_include_path(path, error);
    }

    bool parse_include_path(
        const std::filesystem::path &path,
        std::string &error)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec) && !ec) {
            std::vector<std::filesystem::path> files;
            for (const auto &entry :
                 std::filesystem::directory_iterator(path, ec)) {
                if (ec) {
                    break;
                }
                if (entry.is_regular_file(ec) && !ec) {
                    files.emplace_back(entry.path());
                }
                ec.clear();
            }
            if (ec) {
                return fail(
                    "unable to enumerate included configuration directory " +
                        path.string() + ": " + ec.message(),
                    error);
            }
            std::sort(files.begin(), files.end());
            for (const auto &file : files) {
                if (!read_and_parse_file(
                        file, state_, error)) {
                    return false;
                }
            }
            return true;
        }
        if (ec) {
            return fail(
                "unable to inspect included configuration path " +
                    path.string() + ": " + ec.message(),
                error);
        }
        return read_and_parse_file(path, state_, error);
    }

    bool fail(
        const std::string &detail,
        std::string &error) const
    {
        error = origin_ + ": " + detail +
            " near byte " + std::to_string(position_) + ".";
        return false;
    }

    std::string_view content_;
    std::string origin_;
    ParseState &state_;
    std::filesystem::path include_base_;
    std::size_t position_{0U};
};

bool read_file_text(
    const std::filesystem::path &path,
    std::string &content,
    std::string &error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error =
            "Unable to read APT configuration file: " +
            path.string();
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error =
            "Unable to finish reading APT configuration file: " +
            path.string();
        return false;
    }
    content = stream.str();
    return true;
}

bool Parser::read_and_parse_file(
    const std::filesystem::path &path,
    ParseState &state,
    std::string &error)
{
    std::error_code ec;
    std::filesystem::path identity =
        std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        identity = std::filesystem::absolute(path, ec);
        if (ec) {
            identity = path.lexically_normal();
        }
    }
    if (std::find(
            state.include_stack.begin(),
            state.include_stack.end(),
            identity) != state.include_stack.end()) {
        error =
            "Recursive APT configuration #include detected: " +
            path.string();
        return false;
    }

    std::string content;
    if (!read_file_text(path, content, error)) {
        return false;
    }

    state.include_stack.push_back(identity);
    Parser parser(
        content,
        path.string(),
        state,
        path.parent_path());
    const bool ok = parser.parse(error);
    state.include_stack.pop_back();
    return ok;
}

bool apt_fragment_filename(
    const std::filesystem::path &path)
{
    const std::string name = path.filename().string();
    if (name.empty() || name.front() == '.') {
        return false;
    }
    for (const char raw : name) {
        const unsigned char ch =
            static_cast<unsigned char>(raw);
        if (std::isalnum(ch) == 0 &&
            ch != '-' && ch != '_' && ch != '.') {
            return false;
        }
    }
    const std::string extension =
        path.extension().string();
    return extension.empty() || extension == ".conf";
}

std::filesystem::path rooted_path(
    const std::map<std::string, std::string> &values,
    const std::filesystem::path &path)
{
    const auto root = values.find("rootdir");
    if (root == values.end() || root->second.empty()) {
        return path;
    }

    std::filesystem::path relative = path;
    if (relative.is_absolute()) {
        relative = relative.relative_path();
    }
    return std::filesystem::path(root->second) / relative;
}

std::filesystem::path configured_path(
    const std::map<std::string, std::string> &values,
    const std::string_view key,
    const std::filesystem::path &fallback)
{
    const auto configured =
        values.find(lower_ascii(std::string(key)));
    std::filesystem::path path =
        configured == values.end() || configured->second.empty()
            ? fallback
            : std::filesystem::path(configured->second);

    if (path.is_relative()) {
        const auto etc = values.find("dir::etc");
        const std::filesystem::path base =
            etc == values.end() || etc->second.empty()
                ? std::filesystem::path("/etc/apt")
                : std::filesystem::path(etc->second);
        path = base / path;
    }
    return rooted_path(values, path);
}

bool read_optional_file(
    const std::filesystem::path &path,
    ParseState &state,
    std::string &error)
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        error =
            "Unable to inspect APT configuration file " +
            path.string() + ": " + ec.message();
        return false;
    }
    if (!exists) {
        return true;
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        error =
            "APT configuration path is not a regular file: " +
            path.string();
        return false;
    }
    return Parser::read_and_parse_file(path, state, error);
}

bool read_parts(
    const std::filesystem::path &directory,
    ParseState &state,
    std::string &error)
{
    std::error_code ec;
    const bool exists =
        std::filesystem::exists(directory, ec);
    if (ec) {
        error =
            "Unable to inspect APT configuration fragment directory " +
            directory.string() + ": " + ec.message();
        return false;
    }
    if (!exists) {
        return true;
    }
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        error =
            "APT configuration fragment path is not a directory: " +
            directory.string();
        return false;
    }

    std::vector<std::filesystem::path> files;
    for (const auto &entry :
         std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file(ec) && !ec &&
            apt_fragment_filename(entry.path())) {
            files.emplace_back(entry.path());
        }
        ec.clear();
    }
    if (ec) {
        error =
            "Unable to enumerate APT configuration fragments: " +
            ec.message();
        return false;
    }
    std::sort(files.begin(), files.end());
    for (const auto &file : files) {
        if (!Parser::read_and_parse_file(
                file, state, error)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<DebianAptConfiguration>
DebianAptConfiguration::parse(
    const std::string_view content,
    const std::string_view origin,
    std::string &error)
{
    error.clear();
    ParseState state;
    Parser parser(
        content,
        std::string(origin),
        state,
        {});
    if (!parser.parse(error)) {
        return std::nullopt;
    }
    DebianAptConfiguration result;
    result.values_ = std::move(state.values);
    return result;
}

std::optional<DebianAptConfiguration>
DebianAptConfiguration::read(std::string &error)
{
    error.clear();
    ParseState state;

    if (const char *path = std::getenv("APT_CONFIG");
        path != nullptr && *path != '\0') {
        if (!Parser::read_and_parse_file(
                path, state, error)) {
            return std::nullopt;
        }
    }

    const std::filesystem::path parts = configured_path(
        state.values,
        "Dir::Etc::Parts",
        "apt.conf.d");
    const std::filesystem::path main = configured_path(
        state.values,
        "Dir::Etc::Main",
        "apt.conf");
    if (!read_parts(parts, state, error) ||
        !read_optional_file(main, state, error)) {
        return std::nullopt;
    }

    const std::string binary_prefix =
        "binary::apt-get::";
    std::vector<std::pair<std::string, std::string>>
        binary_overrides;
    for (const auto &[key, value] : state.values) {
        if (key.rfind(binary_prefix, 0U) == 0U &&
            key.size() > binary_prefix.size()) {
            binary_overrides.emplace_back(
                key.substr(binary_prefix.size()),
                value);
        }
    }
    for (const auto &[key, value] : binary_overrides) {
        state.values[key] = value;
    }

    DebianAptConfiguration result;
    result.values_ = std::move(state.values);
    return result;
}

std::optional<std::string>
DebianAptConfiguration::value(
    const std::string_view key) const
{
    const auto found =
        values_.find(lower_ascii(std::string(key)));
    if (found == values_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::filesystem::path DebianAptConfiguration::path(
    const std::string_view key,
    const std::filesystem::path &fallback) const
{
    return configured_path(values_, key, fallback);
}

} // namespace infiltrator::software
