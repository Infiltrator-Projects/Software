// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_phased_updates.hpp"

#include <cassert>
#include <random>
#include <string>

namespace {

infiltrator::software::DebianPackageVersion phased_package(
    const int percentage)
{
    infiltrator::software::DebianPackageVersion package;
    package.package = "binary-name";
    package.version = "2.0-1";
    package.source_package = "source-name";
    package.source_version = "2.0-1";
    package.phased_update_percentage = percentage;
    package.pin_priority = 500;
    return package;
}

unsigned int bucket_for(
    const std::string &machine_id)
{
    const std::string seed_text =
        "source-name-2.0-1-" + machine_id;
    std::seed_seq seed(
        seed_text.begin(),
        seed_text.end());
    std::minstd_rand generator(seed);
    std::uniform_int_distribution<unsigned int>
        distribution(0U, 100U);
    return distribution(generator);
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    const auto always =
        DebianPhasedUpdatesPolicy::for_machine(
            "fixture-machine", true, false);
    assert(!always.evaluate(phased_package(0)).has_value());

    const auto never =
        DebianPhasedUpdatesPolicy::for_machine(
            "fixture-machine", false, true);
    const auto never_decision =
        never.evaluate(phased_package(99));
    assert(never_decision.has_value());
    assert(never_decision->priority == 1);
    assert(never_decision->provider ==
           "debian-phased-updates");

    const auto normal =
        DebianPhasedUpdatesPolicy::for_machine(
            "fixture-machine", false, false);
    assert(!normal.evaluate(phased_package(100)).has_value());
    assert(!normal.evaluate(phased_package(-1)).has_value());

    const unsigned int bucket =
        bucket_for("fixture-machine");
    const auto decision =
        normal.evaluate(phased_package(50));
    if (bucket > 50U) {
        assert(decision.has_value());
        assert(decision->priority == 1);
    } else {
        assert(!decision.has_value());
    }

    return 0;
}
