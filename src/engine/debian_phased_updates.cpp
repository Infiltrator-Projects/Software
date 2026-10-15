// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_phased_updates.hpp"

#include "core/text.hpp"
#include "engine/debian_apt_configuration.hpp"

#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <utility>

namespace infiltrator::software {
namespace {

using text::lower_ascii;
using text::trim;

bool bool_value(
    const std::string_view value,
    bool &parsed)
{
    const std::string text = lower_ascii(trim(value));
    if (text == "1" || text == "true" ||
        text == "yes" || text == "on") {
        parsed = true;
        return true;
    }
    if (text == "0" || text == "false" ||
        text == "no" || text == "off") {
        parsed = false;
        return true;
    }
    return false;
}

bool configured_boolean(
    const DebianAptConfiguration &configuration,
    const std::string_view key,
    bool &value,
    std::string &error)
{
    const auto configured = configuration.value(key);
    if (!configured.has_value()) {
        return true;
    }
    if (!bool_value(*configured, value)) {
        error =
            "APT configuration option " +
            std::string(key) +
            " has an invalid boolean value.";
        return false;
    }
    return true;
}

std::string read_machine_id()
{
    std::ifstream input("/etc/machine-id");
    std::string value;
    if (input) {
        std::getline(input, value);
    }
    return trim(value);
}

} // namespace

DebianPhasedUpdatesPolicy
DebianPhasedUpdatesPolicy::read(std::string &error)
{
    error.clear();
    const auto configuration =
        DebianAptConfiguration::read(error);
    if (!configuration.has_value()) {
        return {};
    }

    std::string machine_id =
        configuration->value("APT::Machine-ID")
            .value_or(std::string{});
    bool always_include = false;
    bool never_include = false;

    if (!configured_boolean(
            *configuration,
            "APT::Get::Always-Include-Phased-Updates",
            always_include,
            error) ||
        !configured_boolean(
            *configuration,
            "APT::Get::Never-Include-Phased-Updates",
            never_include,
            error)) {
        return {};
    }

    // Mint Update Manager historically exposes aliases for the same policy.
    // When present they are later compatibility inputs and therefore override
    // the generic APT values, matching the previous Software behaviour.
    if (!configured_boolean(
            *configuration,
            "Update-Manager::Always-Include-Phased-Updates",
            always_include,
            error) ||
        !configured_boolean(
            *configuration,
            "Update-Manager::Never-Include-Phased-Updates",
            never_include,
            error)) {
        return {};
    }

    if (machine_id.empty()) {
        machine_id = read_machine_id();
    }

    return for_machine(
        std::move(machine_id),
        always_include,
        never_include);
}

DebianPhasedUpdatesPolicy
DebianPhasedUpdatesPolicy::for_machine(
    std::string machine_id,
    const bool always_include,
    const bool never_include)
{
    DebianPhasedUpdatesPolicy policy;
    policy.machine_id_ = std::move(machine_id);
    policy.always_include_ = always_include;
    policy.never_include_ = never_include;
    return policy;
}

std::optional<DebianPolicyDecision>
DebianPhasedUpdatesPolicy::evaluate(
    const DebianPackageVersion &package) const
{
    if (package.phased_update_percentage < 0 ||
        package.phased_update_percentage >= 100 ||
        always_include_) {
        return std::nullopt;
    }

    if (never_include_) {
        return DebianPolicyDecision{
            1,
            std::string(id()),
            "Phased update is held until rollout reaches 100%."};
    }

    if (machine_id_.empty()) {
        return std::nullopt;
    }

    const std::string source_package =
        package.source_package.empty()
            ? package.package
            : package.source_package;
    const std::string source_version =
        package.source_version.empty()
            ? package.version
            : package.source_version;
    const std::string seed_text =
        source_package + "-" +
        source_version + "-" +
        machine_id_;

    std::seed_seq seed(
        seed_text.begin(),
        seed_text.end());
    std::minstd_rand generator(seed);
    std::uniform_int_distribution<unsigned int>
        distribution(0U, 99U);
    const unsigned int bucket =
        distribution(generator);

    if (bucket <
        static_cast<unsigned int>(
            package.phased_update_percentage)) {
        return std::nullopt;
    }

    return DebianPolicyDecision{
        1,
        std::string(id()),
        "Held back by Debian/Ubuntu phased rollout (" +
            std::to_string(
                package.phased_update_percentage) +
            "% rollout; this machine bucket " +
            std::to_string(bucket) + ")."};
}

} // namespace infiltrator::software
