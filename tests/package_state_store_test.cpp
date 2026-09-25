// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/package_state_store.hpp"

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>
#include <sqlite3.h>

namespace {

std::filesystem::path make_temp_directory()
{
    std::string pattern =
        (std::filesystem::temp_directory_path() /
         "infiltrator-software-state-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    char *created = mkdtemp(writable.data());
    assert(created != nullptr);
    return std::filesystem::path(created);
}

infiltrator::software::PackageRecord installed(
    const std::string &name,
    const std::string &version)
{
    using namespace infiltrator::software;
    PackageRecord package;
    package.id = name;
    package.name = name;
    package.package_name = name;
    package.architecture = "amd64";
    package.installed_version = version;
    package.available_version = version;
    package.installed_size_bytes = 4096U;
    package.source = "Debian";
    package.depends = "libcore (>= 1.0)";
    package.pre_depends = "init-base";
    package.provides = "virtual-" + name + " (= " + version + ")";
    package.priority = "optional";
    package.multi_arch = "same";
    package.essential = name == "alpha";
    package.state = InstallState::installed;
    return package;
}

infiltrator::software::DebianPackageVersion available(
    const std::string &name,
    const std::string &version)
{
    using namespace infiltrator::software;
    DebianPackageVersion package;
    package.package = name;
    package.version = version;
    package.architecture = "amd64";
    package.filename =
        "pool/" + name + "_" + version + "_amd64.deb";
    package.sha256 = "deadbeef";
    package.source = "stable";
    package.priority = "optional";
    package.pin_priority = 700;
    package.policy_provider = "host-apt-preferences";
    package.policy_reason = "Matched host APT generic preference.";
    package.release_origin = "linuxmint";
    package.site = "packages.linuxmint.com";
    package.depends = "libc6 (>= 2.38)";
    package.size_bytes = 1024U;
    package.installed_size_bytes = 2048U;
    return package;
}

bool execute_sql(
    const std::filesystem::path &path,
    const char *sql)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open(path.c_str(), &database) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return false;
    }
    char *message = nullptr;
    const int status =
        sqlite3_exec(database, sql, nullptr, nullptr, &message);
    sqlite3_free(message);
    sqlite3_close(database);
    return status == SQLITE_OK;
}

int database_user_version(
    const std::filesystem::path &path)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(
            path.c_str(), &database,
            SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return -1;
    }
    sqlite3_stmt *statement = nullptr;
    int version = -1;
    if (sqlite3_prepare_v2(
            database, "PRAGMA user_version;", -1,
            &statement, nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW) {
        version = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return version;
}

bool table_has_column(
    const std::filesystem::path &path,
    const std::string &column)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(
            path.c_str(), &database,
            SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return false;
    }
    sqlite3_stmt *statement = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(
            database,
            "PRAGMA table_info(available_packages);",
            -1, &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const auto *name = sqlite3_column_text(statement, 1);
            if (name != nullptr &&
                column == reinterpret_cast<const char *>(name)) {
                found = true;
                break;
            }
        }
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return found;
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
    assert(error.empty());

    std::uint64_t generation = 0U;
    assert(store.publish(
        {installed("alpha", "1.0")},
        {available("alpha", "1.1")},
        "fixture-a",
        generation,
        error));
    assert(error.empty());
    assert(generation == 1U);

    auto first = store.load_current(error);
    assert(first.has_value());
    assert(error.empty());
    assert(first->generation == 1U);
    assert(first->source_fingerprint == "fixture-a");
    assert(first->installed.size() == 1U);
    assert(first->available.size() == 1U);
    assert(first->available[0].pin_priority == 700);
    assert(first->available[0].policy_provider ==
           "host-apt-preferences");
    assert(first->available[0].policy_reason ==
           "Matched host APT generic preference.");
    assert(first->available[0].release_origin == "linuxmint");
    assert(first->available[0].site == "packages.linuxmint.com");
    assert(first->installed[0].depends == "libcore (>= 1.0)");
    assert(first->installed[0].pre_depends == "init-base");
    assert(first->installed[0].provides == "virtual-alpha (= 1.0)");
    assert(first->installed[0].priority == "optional");
    assert(first->installed[0].multi_arch == "same");
    assert(first->installed[0].essential);

    assert(store.publish(
        {installed("alpha", "1.1"),
         installed("beta", "2.0")},
        {available("alpha", "1.2"),
         available("beta", "2.1")},
        "fixture-b",
        generation,
        error));
    assert(generation == 2U);

    PackageStateStore reopened(database_path.string());
    auto second = reopened.load_current(error);
    assert(second.has_value());
    assert(second->generation == 2U);
    assert(second->source_fingerprint == "fixture-b");
    assert(second->installed.size() == 2U);
    assert(second->available.size() == 2U);

    const DebianPackageVersion duplicate =
        available("duplicate", "1.0");
    std::uint64_t rejected_generation = 999U;
    assert(!store.publish(
        {installed("alpha", "1.1")},
        {duplicate, duplicate},
        "broken",
        rejected_generation,
        error));
    assert(rejected_generation == 0U);
    assert(!error.empty());

    auto after_failure = store.load_current(error);
    assert(after_failure.has_value());
    assert(error.empty());
    assert(after_failure->generation == 2U);
    assert(after_failure->source_fingerprint == "fixture-b");

    /*
     * A failed schema migration must be all-or-nothing. This version-3
     * fixture already contains the final site column, forcing failure after
     * earlier ALTER TABLE statements; those earlier changes must roll back.
     */
    const std::filesystem::path broken_migration_path =
        directory / "broken-migration.db";
    assert(execute_sql(
        broken_migration_path,
        "CREATE TABLE available_packages ("
        " generation_id INTEGER NOT NULL,"
        " package_name TEXT NOT NULL,"
        " version TEXT NOT NULL,"
        " architecture TEXT NOT NULL,"
        " filename TEXT NOT NULL,"
        " sha256 TEXT NOT NULL,"
        " source TEXT NOT NULL,"
        " priority TEXT NOT NULL,"
        " pin_priority INTEGER NOT NULL DEFAULT 0,"
        " site TEXT NOT NULL DEFAULT '',"
        " multi_arch TEXT NOT NULL,"
        " depends_text TEXT NOT NULL,"
        " pre_depends TEXT NOT NULL,"
        " recommends TEXT NOT NULL,"
        " provides TEXT NOT NULL,"
        " conflicts TEXT NOT NULL,"
        " breaks_text TEXT NOT NULL,"
        " replaces TEXT NOT NULL,"
        " description TEXT NOT NULL,"
        " size_bytes INTEGER NOT NULL,"
        " installed_size_bytes INTEGER NOT NULL,"
        " essential INTEGER NOT NULL"
        ");"
        "PRAGMA user_version=3;"));

    PackageStateStore broken_store(
        broken_migration_path.string());
    error.clear();
    assert(!broken_store.initialise(error));
    assert(!error.empty());
    assert(database_user_version(broken_migration_path) == 3);
    assert(!table_has_column(
        broken_migration_path, "policy_provider"));

    std::filesystem::remove_all(directory);
    return 0;
}
