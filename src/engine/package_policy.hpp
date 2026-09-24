// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_PACKAGE_POLICY_HPP
#define INFILTRATOR_SOFTWARE_PACKAGE_POLICY_HPP

#include "engine/debian_package_index.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct DebianPolicyDecision {
    int priority{0};
    std::string provider;
    std::string reason;
};

class DebianPackagePolicyProvider {
public:
    virtual ~DebianPackagePolicyProvider() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    [[nodiscard]] virtual std::optional<DebianPolicyDecision> evaluate(
        const DebianPackageVersion &package) const = 0;
};

class DebianPolicyStack final {
public:
    void add(const DebianPackagePolicyProvider &provider);

    [[nodiscard]] DebianPolicyDecision evaluate(
        const DebianPackageVersion &package) const;

private:
    std::vector<const DebianPackagePolicyProvider *> providers_;
};

} // namespace infiltrator::software

#endif
