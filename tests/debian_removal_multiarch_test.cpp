// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_transaction.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace {

using infiltrator::software::InstallState;
using infiltrator::software::PackageRecord;
using infiltrator::software::TransactionAction;
using infiltrator::software::TransactionRequest;

PackageRecord installed(
    const std::string &name,
    const std::string &architecture)
{
    PackageRecord package;
    package.id = name + ":" + architecture;
    package.name = name;
    package.package_name = name;
    package.architecture = architecture;
    package.installed_version = "1.0";
    package.available_version = "1.0";
    package.state = InstallState::installed;
    return package;
}

bool removal_is_safe(
    std::vector<PackageRecord> packages,
    const std::string &dependency,
    const std::string &foreign_multi_arch,
    std::string &error)
{
    PackageRecord consumer = installed("consumer", "amd64");
    consumer.depends = dependency;

    PackageRecord native = installed("library", "amd64");
    PackageRecord foreign = installed("library", "arm64");
    foreign.multi_arch = foreign_multi_arch;

    packages = {consumer, native, foreign};

    TransactionRequest request;
    request.action = TransactionAction::remove;
    request.package_ids = {"library:amd64"};

    return infiltrator::software::DebianTransactionPlanner::plan(
               request,
               packages,
               {},
               "amd64",
               1U,
               "multiarch-fixture",
               {},
               error)
        .has_value();
}

} // namespace

int main()
{
    std::string error;

    // An unqualified dependency may use a foreign architecture only when the
    // installed package explicitly advertises Multi-Arch: foreign.
    assert(!removal_is_safe({}, "library", "", error));
    assert(error.find("invalid retained package state") != std::string::npos);

    error.clear();
    assert(removal_is_safe({}, "library", "foreign", error));
    assert(error.empty());

    // :any is a separate contract: a cross-architecture provider must declare
    // Multi-Arch: allowed or foreign. Merely having the package installed for
    // another architecture does not make the retained system satisfiable.
    error.clear();
    assert(!removal_is_safe({}, "library:any", "", error));
    assert(error.find("invalid retained package state") != std::string::npos);

    error.clear();
    assert(removal_is_safe({}, "library:any", "allowed", error));
    assert(error.empty());

    return 0;
}
