// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_PREFERENCES_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_PREFERENCES_HPP

#include "engine/package_policy.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {

enum class DebianPinKind {
    any,
    origin,
    release,
    version
};

struct DebianPreferenceRule {
    std::vector<std::string> packages;
    DebianPinKind kind{DebianPinKind::any};
    std::string pattern;
    std::vector<std::pair<char, std::string>> release_conditions;
    int priority{0};
    bool generic{false};
};

class DebianAptPreferences final : public DebianPackagePolicyProvider {
public:
    static DebianAptPreferences read(std::string &error);

    static DebianAptPreferences parse(
        std::string_view content,
        std::string_view origin,
        std::string &error);

    void append(DebianAptPreferences other);

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return "host-apt-preferences";
    }

    [[nodiscard]] std::optional<DebianPolicyDecision> evaluate(
        const DebianPackageVersion &package) const override;

    [[nodiscard]] bool empty() const noexcept
    {
        return rules_.empty();
    }

private:
    std::vector<DebianPreferenceRule> rules_;
};

} // namespace infiltrator::software

#endif
