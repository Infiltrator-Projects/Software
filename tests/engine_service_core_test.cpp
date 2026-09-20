// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/engine_service_core.hpp"
#include "engine/package_state_store.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

std::filesystem::path make_temp_directory()
{
    std::string pattern =
        (std::filesystem::temp_directory_path() /
         "infiltrator-software-engine-core-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    char *created = mkdtemp(writable.data());
    assert(created != nullptr);
    return std::filesystem::path(created);
}

infiltrator::software::PackageRecord installed(
    const std::string &name,
    const std::string &version,
    const std::uint64_t size)
{
    using namespace infiltrator::software;
    PackageRecord package;
    package.id = name;
    package.name = name;
    package.package_name = name;
    package.architecture = "amd64";
    package.installed_version = version;
    package.available_version = version;
    package.installed_size_bytes = size;
    package.source = "Debian";
    package.state = InstallState::installed;
    return package;
}

infiltrator::software::DebianPackageVersion available(
    const std::string &name,
    const std::string &version,
    const std::uint64_t download,
    const std::uint64_t installed_size)
{
    using namespace infiltrator::software;
    DebianPackageVersion package;
    package.package = name;
    package.version = version;
    package.architecture = "amd64";
    package.source = "stable";
    package.filename =
        "pool/" + name + "_" + version + "_amd64.deb";
    package.sha256 = "0123456789abcdef";
    package.size_bytes = download;
    package.installed_size_bytes = installed_size;
    return package;
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    const std::filesystem::path directory =
        make_temp_directory();
    const std::filesystem::path database_path =
        directory / "packages.db";

    PackageStateStore store(database_path.string());
    std::string error;
    assert(store.initialise(error));

    DebianPackageVersion app =
        available("app", "2.0", 100U, 2000U);
    app.depends = "libcore (>= 2.0)";

    DebianPackageVersion library =
        available("libcore", "2.0", 50U, 1000U);
    library.essential = true;

    std::uint64_t generation = 0U;
    assert(store.publish(
        {installed("app", "1.0", 1500U),
         installed("libcore", "1.0", 800U)},
        {app, library},
        "fixture-generation",
        generation,
        error));
    assert(generation == 1U);

    EngineServiceCore core(database_path.string());
    assert(core.reload(error));
    assert(error.empty());

    const EngineServiceStatus status = core.status();
    assert(status.healthy);
    assert(status.generation == 1U);
    assert(status.installed_count == 2U);
    assert(status.available_count == 2U);
    assert(status.update_count == 2U);
    assert(status.source_fingerprint == "fixture-generation");

    const auto updates = core.updates();
    assert(updates.size() == 2U);

    TransactionRequest request;
    request.action = TransactionAction::upgrade;
    request.package_ids = {"app"};

    const auto plan =
        core.plan(request, "amd64", error);
    assert(plan.has_value());
    assert(error.empty());
    assert(plan->state_generation == 1U);
    assert(plan->source_fingerprint == "fixture-generation");
    assert(plan->items.size() == 2U);
    assert(plan->download_bytes == 150U);
    assert(plan->touches_system);

    std::filesystem::rename(
        database_path,
        database_path.string() + ".gone");
    assert(!core.reload(error));
    assert(!error.empty());

    const EngineServiceStatus stale = core.status();
    assert(!stale.healthy);
    assert(stale.generation == 1U);
    assert(stale.update_count == 2U);

    error.clear();
    const auto blocked =
        core.plan(request, "amd64", error);
    assert(!blocked.has_value());
    assert(!error.empty());

    std::filesystem::remove_all(directory);
    return 0;
}
