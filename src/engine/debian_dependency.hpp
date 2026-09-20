// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_DEPENDENCY_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_DEPENDENCY_HPP

#include "core/model.hpp"
#include "engine/debian_candidate.hpp"
#include "engine/debian_package_index.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

enum class DebianVersionRelation {
    any,
    less,
    less_equal,
    equal,
    greater_equal,
    greater
};

struct DebianDependencyAlternative {
    std::string package;
    std::string architecture_qualifier;
    DebianVersionRelation relation{DebianVersionRelation::any};
    std::string version;
};

struct DebianDependencyGroup {
    std::vector<DebianDependencyAlternative> alternatives;
};

struct DebianDependencyExpression {
    std::vector<DebianDependencyGroup> groups;
};

struct DebianResolutionProblem {
    std::string package;
    std::string expression;
    std::string detail;
};

struct DebianResolution {
    std::vector<DebianPackageVersion> selected;
    std::vector<DebianResolutionProblem> problems;

    [[nodiscard]] bool complete() const noexcept
    {
        return problems.empty();
    }
};

class DebianDependencyResolver final {
public:
    static std::optional<DebianDependencyExpression> parse(
        std::string_view expression,
        std::string &error);

    static DebianResolution resolve(
        const std::vector<DebianPackageVersion> &roots,
        const std::vector<PackageRecord> &installed,
        const std::vector<DebianPackageVersion> &available,
        std::string_view target_architecture,
        const DebianCandidatePolicy &policy = {});
};

} // namespace infiltrator::software

#endif
