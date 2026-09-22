// SPDX-License-Identifier: GPL-3.0-or-later
#include "apt_plan_guard.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software::helper {
namespace {

struct ExactPackage {
    std::string name;
    std::string architecture;
    std::string version;
};

std::string_view trim(std::string_view value)
{
    while (!value.empty() &&
           (value.front() == ' ' || value.front() == '\t' ||
            value.front() == '\r' || value.front() == '\n')) {
        value.remove_prefix(1U);
    }
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t' ||
            value.back() == '\r' || value.back() == '\n')) {
        value.remove_suffix(1U);
    }
    return value;
}

bool split_identity(
    const std::string_view identity,
    std::string &name,
    std::string &architecture)
{
    const std::size_t colon = identity.find(':');
    if (colon == std::string_view::npos) {
        name.assign(identity);
        architecture.clear();
        return !name.empty();
    }
    if (colon == 0U || colon + 1U >= identity.size() ||
        identity.find(':', colon + 1U) != std::string_view::npos) {
        return false;
    }
    name.assign(identity.substr(0U, colon));
    architecture.assign(identity.substr(colon + 1U));
    return true;
}

bool parse_exact_spec(const std::string_view spec, ExactPackage &package)
{
    const std::size_t equals = spec.find('=');
    if (equals == std::string_view::npos || equals == 0U ||
        equals + 1U >= spec.size() ||
        spec.find('=', equals + 1U) != std::string_view::npos) {
        return false;
    }
    if (!split_identity(
            spec.substr(0U, equals), package.name, package.architecture)) {
        return false;
    }
    package.version.assign(spec.substr(equals + 1U));
    return !package.version.empty();
}

bool parse_install_line(const std::string_view line, ExactPackage &package)
{
    constexpr std::string_view prefix = "Inst ";
    if (line.rfind(prefix, 0U) != 0U) return false;

    const std::size_t identity_start = prefix.size();
    const std::size_t identity_end = line.find(' ', identity_start);
    if (identity_end == std::string_view::npos ||
        identity_end <= identity_start ||
        !split_identity(
            line.substr(identity_start, identity_end - identity_start),
            package.name, package.architecture)) {
        return false;
    }

    const std::size_t version_open = line.find('(', identity_end);
    if (version_open == std::string_view::npos ||
        version_open + 1U >= line.size()) {
        return false;
    }
    const std::size_t version_end =
        line.find_first_of(" )", version_open + 1U);
    if (version_end == std::string_view::npos ||
        version_end <= version_open + 1U) {
        return false;
    }
    package.version.assign(
        line.substr(version_open + 1U, version_end - version_open - 1U));

    if (package.architecture.empty()) {
        const std::size_t arch_close = line.rfind(']');
        const std::size_t arch_open = line.rfind('[');
        if (arch_open != std::string_view::npos &&
            arch_close != std::string_view::npos &&
            arch_open > version_open && arch_close > arch_open + 1U) {
            const std::string_view architecture =
                trim(line.substr(
                    arch_open + 1U, arch_close - arch_open - 1U));
            if (!architecture.empty() && architecture != "all") {
                package.architecture.assign(architecture);
            }
        }
    }
    return true;
}

bool compatible(const ExactPackage &approved, const ExactPackage &actual)
{
    if (approved.name != actual.name ||
        approved.version != actual.version) {
        return false;
    }
    if (approved.architecture.empty()) return true;
    return approved.architecture == actual.architecture;
}

std::string render(const ExactPackage &package)
{
    std::string result = package.name;
    if (!package.architecture.empty()) result += ":" + package.architecture;
    result += "=" + package.version;
    return result;
}

} // namespace

bool validate_apt_simulation(
    const std::vector<std::string> &approved_specs,
    const std::string_view simulation_output,
    std::string &error)
{
    error.clear();
    if (approved_specs.empty()) {
        error = "The approved package plan is empty.";
        return false;
    }

    std::vector<ExactPackage> approved;
    approved.reserve(approved_specs.size());
    for (const std::string &spec : approved_specs) {
        ExactPackage package;
        if (!parse_exact_spec(spec, package)) {
            error = "Invalid approved package specification: " + spec + ".";
            return false;
        }
        for (const ExactPackage &existing : approved) {
            if (existing.name == package.name &&
                existing.architecture == package.architecture) {
                error =
                    "The approved plan contains the same package identity more than once: " +
                    package.name + ".";
                return false;
            }
        }
        approved.emplace_back(std::move(package));
    }

    std::vector<ExactPackage> actual;
    std::size_t start = 0U;
    while (start <= simulation_output.size()) {
        const std::size_t end = simulation_output.find('\n', start);
        const std::string_view line = trim(simulation_output.substr(
            start,
            end == std::string_view::npos
                ? simulation_output.size() - start
                : end - start));

        if (line.rfind("Remv ", 0U) == 0U) {
            error =
                "Post-refresh APT simulation would remove a package; the approved plan is invalid.";
            return false;
        }
        if (line.rfind("Inst ", 0U) == 0U) {
            ExactPackage package;
            if (!parse_install_line(line, package)) {
                error =
                    "Unable to parse post-refresh APT simulation line: " +
                    std::string(line) + ".";
                return false;
            }
            actual.emplace_back(std::move(package));
        }

        if (end == std::string_view::npos) break;
        start = end + 1U;
    }

    if (actual.empty()) {
        error =
            "Post-refresh APT simulation contains no package changes; the approved plan is stale.";
        return false;
    }

    std::vector<bool> matched(actual.size(), false);
    for (const ExactPackage &expected : approved) {
        bool found = false;
        for (std::size_t index = 0U; index < actual.size(); ++index) {
            if (!matched[index] && compatible(expected, actual[index])) {
                matched[index] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            error =
                "Post-refresh APT simulation no longer contains approved change " +
                render(expected) + ".";
            return false;
        }
    }

    for (std::size_t index = 0U; index < actual.size(); ++index) {
        if (!matched[index]) {
            error =
                "Post-refresh APT simulation introduced an unapproved change " +
                render(actual[index]) + ".";
            return false;
        }
    }
    return true;
}

} // namespace infiltrator::software::helper
