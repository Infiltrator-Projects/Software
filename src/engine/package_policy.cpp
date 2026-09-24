// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/package_policy.hpp"

#include <string>

namespace infiltrator::software {

void DebianPolicyStack::add(
    const DebianPackagePolicyProvider &provider)
{
    providers_.push_back(&provider);
}

DebianPolicyDecision DebianPolicyStack::evaluate(
    const DebianPackageVersion &package) const
{
    /*
     * Providers are ordered from most specific/highest authority to broadest
     * compatibility fallback. This lets an Infiltrator distribution policy
     * eventually override selected packages while the host APT policy
     * continues to protect everything not yet migrated.
     */
    for (const DebianPackagePolicyProvider *provider : providers_) {
        if (provider == nullptr) {
            continue;
        }
        const std::optional<DebianPolicyDecision> decision =
            provider->evaluate(package);
        if (decision.has_value()) {
            DebianPolicyDecision resolved = *decision;
            if (resolved.provider.empty()) {
                resolved.provider =
                    std::string(provider->id());
            }
            return resolved;
        }
    }

    DebianPolicyDecision fallback;
    fallback.priority = package.pin_priority;
    fallback.provider = "repository-default";
    fallback.reason =
        "Repository Release metadata/default Debian priority.";
    return fallback;
}

} // namespace infiltrator::software
