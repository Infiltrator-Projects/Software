// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_dependency.hpp"

#include "engine/debian_version.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

std::vector<std::string_view> split_top_level(
    const std::string_view value,
    const char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0U;
    int parentheses = 0;
    int brackets = 0;
    int angles = 0;

    for (std::size_t index = 0U; index < value.size(); ++index) {
        switch (value[index]) {
        case '(':
            ++parentheses;
            break;
        case ')':
            if (parentheses > 0) {
                --parentheses;
            }
            break;
        case '[':
            ++brackets;
            break;
        case ']':
            if (brackets > 0) {
                --brackets;
            }
            break;
        case '<':
            if (parentheses == 0) {
                ++angles;
            }
            break;
        case '>':
            if (parentheses == 0 && angles > 0) {
                --angles;
            }
            break;
        default:
            break;
        }

        if (value[index] == separator &&
            parentheses == 0 &&
            brackets == 0 &&
            angles == 0) {
            parts.emplace_back(value.substr(start, index - start));
            start = index + 1U;
        }
    }

    parts.emplace_back(value.substr(start));
    return parts;
}

bool valid_package_character(const char value) noexcept
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 ||
           value == '+' || value == '-' || value == '.';
}

std::optional<DebianDependencyAlternative> parse_alternative(
    const std::string_view text,
    std::string &error)
{
    std::string value = trim(text);
    if (value.empty()) {
        error = "Empty dependency alternative.";
        return std::nullopt;
    }

    const std::size_t bracket = value.find(" [");
    if (bracket != std::string::npos) {
        value.erase(bracket);
        value = trim(value);
    }
    const std::size_t profile = value.find(" <");
    if (profile != std::string::npos) {
        value.erase(profile);
        value = trim(value);
    }

    std::size_t name_end = 0U;
    while (name_end < value.size() &&
           !std::isspace(
               static_cast<unsigned char>(value[name_end])) &&
           value[name_end] != '(') {
        ++name_end;
    }

    std::string identity = value.substr(0U, name_end);
    if (identity.empty()) {
        error = "Dependency package name is missing.";
        return std::nullopt;
    }

    DebianDependencyAlternative result;
    const std::size_t colon = identity.find(':');
    if (colon == std::string::npos) {
        result.package = identity;
    } else {
        result.package = identity.substr(0U, colon);
        result.architecture_qualifier = identity.substr(colon + 1U);
        if (result.architecture_qualifier.empty()) {
            error = "Dependency architecture qualifier is empty.";
            return std::nullopt;
        }
    }

    if (result.package.empty() ||
        !std::all_of(
            result.package.begin(),
            result.package.end(),
            valid_package_character)) {
        error = "Invalid dependency package name: " + result.package;
        return std::nullopt;
    }

    const std::size_t open = value.find('(', name_end);
    if (open == std::string::npos) {
        return result;
    }

    const std::size_t close = value.find(')', open + 1U);
    if (close == std::string::npos) {
        error = "Unterminated dependency version constraint.";
        return std::nullopt;
    }

    const std::string constraint =
        trim(std::string_view(value).substr(
            open + 1U, close - open - 1U));
    const std::size_t space = constraint.find_first_of(" 	");
    if (space == std::string::npos) {
        error = "Dependency version constraint is incomplete.";
        return std::nullopt;
    }

    const std::string relation = constraint.substr(0U, space);
    result.version = trim(
        std::string_view(constraint).substr(space + 1U));
    if (result.version.empty()) {
        error = "Dependency version is missing.";
        return std::nullopt;
    }

    if (relation == "<<") {
        result.relation = DebianVersionRelation::less;
    } else if (relation == "<=") {
        result.relation = DebianVersionRelation::less_equal;
    } else if (relation == "=") {
        result.relation = DebianVersionRelation::equal;
    } else if (relation == ">=") {
        result.relation = DebianVersionRelation::greater_equal;
    } else if (relation == ">>") {
        result.relation = DebianVersionRelation::greater;
    } else {
        error = "Unsupported Debian version relation: " + relation;
        return std::nullopt;
    }

    if (!trim(std::string_view(value).substr(close + 1U)).empty()) {
        error = "Unexpected text after dependency constraint.";
        return std::nullopt;
    }

    return result;
}

bool version_matches(
    const std::string_view version,
    const DebianDependencyAlternative &dependency)
{
    if (dependency.relation == DebianVersionRelation::any) {
        return true;
    }

    const int comparison =
        compare_debian_versions(version, dependency.version);
    switch (dependency.relation) {
    case DebianVersionRelation::less:
        return comparison < 0;
    case DebianVersionRelation::less_equal:
        return comparison <= 0;
    case DebianVersionRelation::equal:
        return comparison == 0;
    case DebianVersionRelation::greater_equal:
        return comparison >= 0;
    case DebianVersionRelation::greater:
        return comparison > 0;
    case DebianVersionRelation::any:
        return true;
    }
    return false;
}

std::string base_package(const std::string_view package)
{
    const std::size_t colon = package.find(':');
    return std::string(
        colon == std::string_view::npos
            ? package
            : package.substr(0U, colon));
}

bool installed_matches(
    const PackageRecord &installed,
    const DebianDependencyAlternative &dependency,
    const std::string_view target_architecture)
{
    if (base_package(installed.package_name) != dependency.package) {
        return false;
    }

    if (!dependency.architecture_qualifier.empty() &&
        dependency.architecture_qualifier != "any" &&
        dependency.architecture_qualifier != "native" &&
        installed.architecture != dependency.architecture_qualifier) {
        return false;
    }

    if (dependency.architecture_qualifier == "native" &&
        !target_architecture.empty() &&
        installed.architecture != target_architecture) {
        return false;
    }

    return version_matches(
        installed.installed_version, dependency);
}

bool candidate_architecture_matches(
    const DebianPackageVersion &candidate,
    const DebianDependencyAlternative &dependency,
    const std::string_view target_architecture)
{
    if (candidate.architecture == "all") {
        return true;
    }

    if (dependency.architecture_qualifier == "any") {
        return candidate.multi_arch == "allowed" ||
               candidate.multi_arch == "foreign";
    }

    if (!dependency.architecture_qualifier.empty() &&
        dependency.architecture_qualifier != "native") {
        return candidate.architecture ==
               dependency.architecture_qualifier;
    }

    if (target_architecture.empty()) {
        return true;
    }

    return candidate.architecture == target_architecture ||
           candidate.multi_arch == "foreign";
}

int source_priority(
    const DebianPackageVersion &candidate,
    const DebianCandidatePolicy &policy)
{
    const auto configured =
        policy.source_priorities.find(candidate.source);
    return configured == policy.source_priorities.end()
        ? policy.default_source_priority
        : configured->second;
}

bool package_is_held(
    const std::string_view package,
    const DebianCandidatePolicy &policy)
{
    return policy.held_packages.find(std::string(package)) !=
           policy.held_packages.end();
}

std::optional<DebianDependencyAlternative> provided_identity(
    const std::string_view text)
{
    std::string error;
    return parse_alternative(text, error);
}

bool candidate_provides(
    const DebianPackageVersion &candidate,
    const DebianDependencyAlternative &dependency)
{
    if (candidate.provides.empty()) {
        return false;
    }

    for (const std::string_view item :
         split_top_level(candidate.provides, ',')) {
        const auto provided = provided_identity(item);
        if (!provided.has_value() ||
            provided->package != dependency.package) {
            continue;
        }

        if (dependency.relation == DebianVersionRelation::any) {
            return true;
        }

        if (provided->relation != DebianVersionRelation::equal ||
            provided->version.empty()) {
            continue;
        }
        if (version_matches(provided->version, dependency)) {
            return true;
        }
    }

    return false;
}

bool direct_candidate_matches(
    const DebianPackageVersion &candidate,
    const DebianDependencyAlternative &dependency)
{
    return candidate.package == dependency.package &&
           version_matches(candidate.version, dependency);
}

bool better_candidate(
    const DebianPackageVersion &left,
    const int left_priority,
    const DebianPackageVersion &right,
    const int right_priority,
    const std::string_view target_architecture)
{
    if (left_priority != right_priority) {
        return left_priority > right_priority;
    }

    const int version =
        compare_debian_versions(left.version, right.version);
    if (version != 0) {
        return version > 0;
    }

    const bool left_native =
        !target_architecture.empty() &&
        left.architecture == target_architecture;
    const bool right_native =
        !target_architecture.empty() &&
        right.architecture == target_architecture;
    if (left_native != right_native) {
        return left_native;
    }

    if (left.source != right.source) {
        return left.source < right.source;
    }
    if (left.package != right.package) {
        return left.package < right.package;
    }
    return left.filename < right.filename;
}

const DebianPackageVersion *best_available(
    const DebianDependencyAlternative &dependency,
    const std::vector<DebianPackageVersion> &available,
    const std::string_view target_architecture,
    const DebianCandidatePolicy &policy)
{
    const DebianPackageVersion *best = nullptr;
    int best_priority = 0;

    for (const DebianPackageVersion &candidate : available) {
        if (!candidate_architecture_matches(
                candidate, dependency, target_architecture)) {
            continue;
        }

        const bool matches =
            direct_candidate_matches(candidate, dependency) ||
            candidate_provides(candidate, dependency);
        if (!matches) {
            continue;
        }

        const int priority = source_priority(candidate, policy);
        if (priority <= 0) {
            continue;
        }

        if (best == nullptr ||
            better_candidate(
                candidate, priority,
                *best, best_priority,
                target_architecture)) {
            best = &candidate;
            best_priority = priority;
        }
    }

    return best;
}

std::string selected_key(const DebianPackageVersion &package)
{
    return package.package + ":" + package.architecture;
}

bool selected_satisfies(
    const std::unordered_map<std::string, DebianPackageVersion> &selected,
    const DebianDependencyAlternative &dependency,
    const std::string_view target_architecture)
{
    for (const auto &entry : selected) {
        const DebianPackageVersion &candidate = entry.second;
        if (!candidate_architecture_matches(
                candidate, dependency, target_architecture)) {
            continue;
        }
        if (direct_candidate_matches(candidate, dependency) ||
            candidate_provides(candidate, dependency)) {
            return true;
        }
    }
    return false;
}

bool installed_satisfies(
    const std::vector<PackageRecord> &installed,
    const DebianDependencyAlternative &dependency,
    const std::string_view target_architecture)
{
    return std::any_of(
        installed.begin(),
        installed.end(),
        [&](const PackageRecord &package) {
            return installed_matches(
                package, dependency, target_architecture);
        });
}

std::string relation_text(
    const DebianDependencyAlternative &dependency)
{
    std::string identity = dependency.package;
    if (!dependency.architecture_qualifier.empty()) {
        identity += ":" + dependency.architecture_qualifier;
    }

    switch (dependency.relation) {
    case DebianVersionRelation::any:
        return identity;
    case DebianVersionRelation::less:
        return identity + " (<< " + dependency.version + ")";
    case DebianVersionRelation::less_equal:
        return identity + " (<= " + dependency.version + ")";
    case DebianVersionRelation::equal:
        return identity + " (= " + dependency.version + ")";
    case DebianVersionRelation::greater_equal:
        return identity + " (>= " + dependency.version + ")";
    case DebianVersionRelation::greater:
        return identity + " (>> " + dependency.version + ")";
    }
    return identity;
}

bool relation_hits_package(
    const DebianDependencyAlternative &relation,
    const DebianPackageVersion &package)
{
    return relation.package == package.package &&
           version_matches(package.version, relation);
}

bool relation_hits_installed(
    const DebianDependencyAlternative &relation,
    const PackageRecord &package)
{
    return base_package(package.package_name) == relation.package &&
           version_matches(package.installed_version, relation);
}

void check_conflicts(
    const std::unordered_map<std::string, DebianPackageVersion> &selected,
    const std::vector<PackageRecord> &installed,
    std::vector<DebianResolutionProblem> &problems)
{
    for (const auto &entry : selected) {
        const DebianPackageVersion &owner = entry.second;
        const std::string expressions =
            owner.conflicts.empty()
                ? owner.breaks
                : owner.breaks.empty()
                    ? owner.conflicts
                    : owner.conflicts + ", " + owner.breaks;
        if (expressions.empty()) {
            continue;
        }

        std::string parse_error;
        const auto parsed =
            DebianDependencyResolver::parse(expressions, parse_error);
        if (!parsed.has_value()) {
            problems.push_back({
                owner.package,
                expressions,
                "Invalid Conflicts/Breaks expression: " + parse_error});
            continue;
        }

        for (const DebianDependencyGroup &group : parsed->groups) {
            for (const DebianDependencyAlternative &relation :
                 group.alternatives) {
                for (const auto &other_entry : selected) {
                    const DebianPackageVersion &other =
                        other_entry.second;
                    if (selected_key(other) == selected_key(owner)) {
                        continue;
                    }
                    if (relation_hits_package(relation, other)) {
                        problems.push_back({
                            owner.package,
                            relation_text(relation),
                            "Selected package conflicts with " +
                                other.package + " " + other.version + "."});
                    }
                }

                for (const PackageRecord &other : installed) {
                    if (base_package(other.package_name) == owner.package) {
                        continue;
                    }
                    if (relation_hits_installed(relation, other)) {
                        problems.push_back({
                            owner.package,
                            relation_text(relation),
                            "Selected package conflicts with installed " +
                                other.package_name + " " +
                                other.installed_version + "."});
                    }
                }
            }
        }
    }
}

} // namespace

std::optional<DebianDependencyExpression>
DebianDependencyResolver::parse(
    const std::string_view expression,
    std::string &error)
{
    error.clear();
    DebianDependencyExpression result;

    if (trim(expression).empty()) {
        return result;
    }

    for (const std::string_view group_text :
         split_top_level(expression, ',')) {
        DebianDependencyGroup group;
        for (const std::string_view alternative_text :
             split_top_level(group_text, '|')) {
            const auto alternative =
                parse_alternative(alternative_text, error);
            if (!alternative.has_value()) {
                return std::nullopt;
            }
            group.alternatives.push_back(*alternative);
        }

        if (group.alternatives.empty()) {
            error = "Dependency group contains no alternatives.";
            return std::nullopt;
        }
        result.groups.emplace_back(std::move(group));
    }

    return result;
}

DebianResolution DebianDependencyResolver::resolve(
    const std::vector<DebianPackageVersion> &roots,
    const std::vector<PackageRecord> &installed,
    const std::vector<DebianPackageVersion> &available,
    const std::string_view target_architecture,
    const DebianCandidatePolicy &policy)
{
    DebianResolution result;
    std::unordered_map<std::string, DebianPackageVersion> selected;
    std::vector<std::string> pending;

    for (const DebianPackageVersion &root : roots) {
        const std::string key = selected_key(root);
        if (selected.emplace(key, root).second) {
            pending.push_back(key);
        }
    }

    std::size_t cursor = 0U;
    while (cursor < pending.size()) {
        const std::string key = pending[cursor++];
        const auto found_owner = selected.find(key);
        if (found_owner == selected.end()) {
            continue;
        }
        const DebianPackageVersion owner = found_owner->second;

        const std::string hard_dependencies =
            owner.pre_depends.empty()
                ? owner.depends
                : owner.depends.empty()
                    ? owner.pre_depends
                    : owner.pre_depends + ", " + owner.depends;
        if (hard_dependencies.empty()) {
            continue;
        }

        std::string parse_error;
        const auto dependencies =
            parse(hard_dependencies, parse_error);
        if (!dependencies.has_value()) {
            result.problems.push_back({
                owner.package,
                hard_dependencies,
                "Invalid dependency expression: " + parse_error});
            continue;
        }

        for (const DebianDependencyGroup &group :
             dependencies->groups) {
            bool satisfied = false;

            for (const DebianDependencyAlternative &alternative :
                 group.alternatives) {
                if (selected_satisfies(
                        selected,
                        alternative,
                        target_architecture) ||
                    installed_satisfies(
                        installed,
                        alternative,
                        target_architecture)) {
                    satisfied = true;
                    break;
                }
            }
            if (satisfied) {
                continue;
            }

            const DebianPackageVersion *chosen = nullptr;

            for (const DebianDependencyAlternative &alternative :
                 group.alternatives) {
                if (package_is_held(alternative.package, policy)) {
                    continue;
                }

                const DebianPackageVersion *candidate =
                    best_available(
                        alternative,
                        available,
                        target_architecture,
                        policy);
                if (candidate != nullptr) {
                    chosen = candidate;
                    break;
                }
            }

            if (chosen == nullptr) {
                std::string expression;
                for (std::size_t index = 0U;
                     index < group.alternatives.size();
                     ++index) {
                    if (index != 0U) {
                        expression += " | ";
                    }
                    expression +=
                        relation_text(group.alternatives[index]);
                }

                result.problems.push_back({
                    owner.package,
                    expression,
                    "No installed package or repository candidate "
                    "satisfies this dependency."});
                continue;
            }

            const std::string chosen_key = selected_key(*chosen);
            if (selected.emplace(chosen_key, *chosen).second) {
                pending.push_back(chosen_key);
            }
        }
    }

    check_conflicts(selected, installed, result.problems);

    result.selected.reserve(selected.size());
    for (auto &entry : selected) {
        result.selected.emplace_back(std::move(entry.second));
    }

    std::sort(
        result.selected.begin(),
        result.selected.end(),
        [](const DebianPackageVersion &left,
           const DebianPackageVersion &right) {
            if (left.package != right.package) {
                return left.package < right.package;
            }
            if (left.architecture != right.architecture) {
                return left.architecture < right.architecture;
            }
            return compare_debian_versions(
                       left.version, right.version) > 0;
        });

    std::sort(
        result.problems.begin(),
        result.problems.end(),
        [](const DebianResolutionProblem &left,
           const DebianResolutionProblem &right) {
            if (left.package != right.package) {
                return left.package < right.package;
            }
            if (left.expression != right.expression) {
                return left.expression < right.expression;
            }
            return left.detail < right.detail;
        });
    result.problems.erase(
        std::unique(
            result.problems.begin(),
            result.problems.end(),
            [](const DebianResolutionProblem &left,
               const DebianResolutionProblem &right) {
                return left.package == right.package &&
                       left.expression == right.expression &&
                       left.detail == right.detail;
            }),
        result.problems.end());

    return result;
}

} // namespace infiltrator::software
