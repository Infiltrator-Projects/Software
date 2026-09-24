// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_PHASED_UPDATES_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_PHASED_UPDATES_HPP

#include "engine/package_policy.hpp"

#include <string>

namespace infiltrator::software {

class DebianPhasedUpdatesPolicy final
    : public DebianPackagePolicyProvider {
public:
    static DebianPhasedUpdatesPolicy read(std::string &error);

    static DebianPhasedUpdatesPolicy for_machine(
        std::string machine_id,
        bool always_include = false,
        bool never_include = false);

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return "debian-phased-updates";
    }

    [[nodiscard]] std::optional<DebianPolicyDecision> evaluate(
        const DebianPackageVersion &package) const override;

private:
    std::string machine_id_;
    bool always_include_{false};
    bool never_include_{false};
};

} // namespace infiltrator::software

#endif
