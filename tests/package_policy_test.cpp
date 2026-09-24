// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/package_policy.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <string_view>

namespace {

class FixedPolicy final
    : public infiltrator::software::DebianPackagePolicyProvider {
public:
    FixedPolicy(
        std::string id,
        std::string package,
        const int priority)
        : id_(std::move(id)),
          package_(std::move(package)),
          priority_(priority)
    {
    }

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return id_;
    }

    [[nodiscard]]
    std::optional<infiltrator::software::DebianPolicyDecision>
    evaluate(
        const infiltrator::software::DebianPackageVersion &package)
        const override
    {
        if (package.package != package_) {
            return std::nullopt;
        }

        return infiltrator::software::DebianPolicyDecision{
            priority_,
            id_,
            "Fixture package policy override."};
    }

private:
    std::string id_;
    std::string package_;
    int priority_{0};
};

infiltrator::software::DebianPackageVersion package(
    const std::string &name,
    const int default_priority)
{
    infiltrator::software::DebianPackageVersion result;
    result.package = name;
    result.version = "1.0";
    result.architecture = "amd64";
    result.pin_priority = default_priority;
    return result;
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    const FixedPolicy infiltrator{
        "infiltrator-distribution",
        "migrated-package",
        900};
    const FixedPolicy host{
        "host-compatibility",
        "migrated-package",
        700};
    const FixedPolicy host_only{
        "host-compatibility",
        "host-package",
        650};

    DebianPolicyStack policy;
    policy.add(infiltrator);
    policy.add(host);
    policy.add(host_only);

    const DebianPolicyDecision migrated =
        policy.evaluate(package("migrated-package", 500));
    assert(migrated.priority == 900);
    assert(migrated.provider == "infiltrator-distribution");

    const DebianPolicyDecision compatibility =
        policy.evaluate(package("host-package", 500));
    assert(compatibility.priority == 650);
    assert(compatibility.provider == "host-compatibility");

    const DebianPolicyDecision untouched =
        policy.evaluate(package("ordinary-package", 500));
    assert(untouched.priority == 500);
    assert(untouched.provider == "repository-default");

    return 0;
}
