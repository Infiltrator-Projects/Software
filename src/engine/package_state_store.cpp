// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/package_state_store.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace infiltrator::software {
namespace {

constexpr int kSchemaVersion = 3;

struct Database {
    sqlite3 *handle{nullptr};
    Database() = default;
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    ~Database()
    {
        if (handle != nullptr) {
            sqlite3_close(handle);
        }
    }
};

struct Statement {
    sqlite3_stmt *handle{nullptr};
    Statement() = default;
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    ~Statement()
    {
        if (handle != nullptr) {
            sqlite3_finalize(handle);
        }
    }
};

std::string sqlite_error(sqlite3 *database)
{
    const char *message = sqlite3_errmsg(database);
    return message == nullptr
        ? "Unknown SQLite error."
        : std::string(message);
}

bool exec_sql(
    sqlite3 *database,
    const char *sql,
    std::string &error)
{
    char *message = nullptr;
    const int status =
        sqlite3_exec(database, sql, nullptr, nullptr, &message);
    if (status == SQLITE_OK) {
        sqlite3_free(message);
        return true;
    }

    error = message != nullptr
        ? std::string(message)
        : sqlite_error(database);
    sqlite3_free(message);
    return false;
}

bool prepare(
    sqlite3 *database,
    const char *sql,
    Statement &statement,
    std::string &error)
{
    if (sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement.handle,
            nullptr) != SQLITE_OK) {
        error = sqlite_error(database);
        return false;
    }
    return true;
}

bool bind_text(
    sqlite3_stmt *statement,
    const int index,
    const std::string_view value)
{
    if (value.size() >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return false;
    }
    return sqlite3_bind_text(
               statement,
               index,
               value.data(),
               static_cast<int>(value.size()),
               SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string column_text(
    sqlite3_stmt *statement,
    const int index)
{
    const unsigned char *text =
        sqlite3_column_text(statement, index);
    return text == nullptr
        ? std::string{}
        : std::string(
            reinterpret_cast<const char *>(text));
}

sqlite3_int64 to_sqlite_integer(
    const std::uint64_t value) noexcept
{
    constexpr std::uint64_t maximum =
        static_cast<std::uint64_t>(
            std::numeric_limits<sqlite3_int64>::max());
    return static_cast<sqlite3_int64>(
        std::min(value, maximum));
}

std::uint64_t from_sqlite_unsigned(
    const sqlite3_int64 value) noexcept
{
    return value <= 0
        ? 0U
        : static_cast<std::uint64_t>(value);
}

bool open_database(
    const std::string &path,
    Database &database,
    std::string &error)
{
    error.clear();

    const std::filesystem::path filesystem_path(path);
    if (filesystem_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(
            filesystem_path.parent_path(), ec);
        if (ec) {
            error =
                "Unable to create package-state directory: " +
                ec.message();
            return false;
        }
    }

    if (sqlite3_open_v2(
            path.c_str(),
            &database.handle,
            SQLITE_OPEN_READWRITE |
                SQLITE_OPEN_CREATE |
                SQLITE_OPEN_FULLMUTEX |
                SQLITE_OPEN_NOFOLLOW,
            nullptr) != SQLITE_OK) {
        error = database.handle != nullptr
            ? sqlite_error(database.handle)
            : "Unable to open package-state database.";
        return false;
    }

    sqlite3_busy_timeout(database.handle, 5000);

    return
        exec_sql(
            database.handle,
            "PRAGMA journal_mode=WAL;",
            error) &&
        exec_sql(
            database.handle,
            "PRAGMA synchronous=FULL;",
            error) &&
        exec_sql(
            database.handle,
            "PRAGMA foreign_keys=ON;",
            error) &&
        exec_sql(
            database.handle,
            "PRAGMA trusted_schema=OFF;",
            error);
}

bool open_database_read_only(
    const std::string &path,
    Database &database,
    std::string &error)
{
    error.clear();

    if (sqlite3_open_v2(
            path.c_str(),
            &database.handle,
            SQLITE_OPEN_READONLY |
                SQLITE_OPEN_FULLMUTEX |
                SQLITE_OPEN_NOFOLLOW,
            nullptr) != SQLITE_OK) {
        error = database.handle != nullptr
            ? sqlite_error(database.handle)
            : "Unable to open package-state database for reading.";
        return false;
    }

    sqlite3_busy_timeout(database.handle, 5000);

    return
        exec_sql(
            database.handle,
            "PRAGMA query_only=ON;",
            error) &&
        exec_sql(
            database.handle,
            "PRAGMA trusted_schema=OFF;",
            error);
}

bool read_schema_version(
    sqlite3 *database,
    int &version,
    std::string &error)
{
    Statement statement;
    if (!prepare(
            database,
            "PRAGMA user_version;",
            statement,
            error)) {
        return false;
    }

    if (sqlite3_step(statement.handle) != SQLITE_ROW) {
        error = sqlite_error(database);
        return false;
    }

    version = sqlite3_column_int(statement.handle, 0);
    return true;
}

bool ensure_schema(
    sqlite3 *database,
    std::string &error)
{
    int version = 0;
    if (!read_schema_version(database, version, error)) {
        return false;
    }

    if (version != 0 && version != 1 &&
        version != 2 && version != kSchemaVersion) {
        error =
            "Unsupported package-state schema version " +
            std::to_string(version) + ".";
        return false;
    }

    static constexpr const char *schema =
        "CREATE TABLE IF NOT EXISTS generations ("
        " id INTEGER PRIMARY KEY,"
        " published_at INTEGER NOT NULL,"
        " source_fingerprint TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS current_state ("
        " singleton INTEGER PRIMARY KEY CHECK(singleton = 1),"
        " generation_id INTEGER NOT NULL"
        "   REFERENCES generations(id) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS installed_packages ("
        " generation_id INTEGER NOT NULL"
        "   REFERENCES generations(id) ON DELETE CASCADE,"
        " package_id TEXT NOT NULL,"
        " name TEXT NOT NULL,"
        " package_name TEXT NOT NULL,"
        " architecture TEXT NOT NULL,"
        " installed_version TEXT NOT NULL,"
        " available_version TEXT NOT NULL,"
        " installed_size_bytes INTEGER NOT NULL,"
        " source TEXT NOT NULL,"
        " depends_text TEXT NOT NULL DEFAULT '',"
        " pre_depends TEXT NOT NULL DEFAULT '',"
        " provides TEXT NOT NULL DEFAULT '',"
        " priority TEXT NOT NULL DEFAULT '',"
        " multi_arch TEXT NOT NULL DEFAULT '',"
        " essential INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY(generation_id, package_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS available_packages ("
        " generation_id INTEGER NOT NULL"
        "   REFERENCES generations(id) ON DELETE CASCADE,"
        " package_name TEXT NOT NULL,"
        " version TEXT NOT NULL,"
        " architecture TEXT NOT NULL,"
        " filename TEXT NOT NULL,"
        " sha256 TEXT NOT NULL,"
        " source TEXT NOT NULL,"
        " priority TEXT NOT NULL,"
        " pin_priority INTEGER NOT NULL DEFAULT 0,"
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
        " essential INTEGER NOT NULL,"
        " PRIMARY KEY("
        "  generation_id, package_name, version,"
        "  architecture, source, filename"
        " )"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_available_name"
        " ON available_packages("
        " generation_id, package_name, architecture"
        ");";

    if (!exec_sql(database, schema, error)) {
        return false;
    }

    if (version == 1) {
        static constexpr const char *migration =
            "ALTER TABLE installed_packages ADD COLUMN depends_text TEXT NOT NULL DEFAULT '';"
            "ALTER TABLE installed_packages ADD COLUMN pre_depends TEXT NOT NULL DEFAULT '';"
            "ALTER TABLE installed_packages ADD COLUMN provides TEXT NOT NULL DEFAULT '';"
            "ALTER TABLE installed_packages ADD COLUMN priority TEXT NOT NULL DEFAULT '';"
            "ALTER TABLE installed_packages ADD COLUMN multi_arch TEXT NOT NULL DEFAULT '';"
            "ALTER TABLE installed_packages ADD COLUMN essential INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE available_packages ADD COLUMN pin_priority INTEGER NOT NULL DEFAULT 0;"
            "PRAGMA user_version=3;";
        if (!exec_sql(database, migration, error)) {
            return false;
        }
    } else if (version == 2) {
        static constexpr const char *migration =
            "ALTER TABLE available_packages ADD COLUMN pin_priority INTEGER NOT NULL DEFAULT 0;"
            "PRAGMA user_version=3;";
        if (!exec_sql(database, migration, error)) {
            return false;
        }
    } else if (version == 0) {
        if (!exec_sql(
                database,
                "PRAGMA user_version=3;",
                error)) {
            return false;
        }
    }

    return true;
}

bool open_ready(
    const std::string &path,
    Database &database,
    std::string &error)
{
    return
        open_database(path, database, error) &&
        ensure_schema(database.handle, error);
}

bool open_reader(
    const std::string &path,
    Database &database,
    std::string &error)
{
    if (!open_database_read_only(
            path, database, error)) {
        return false;
    }

    int version = 0;
    if (!read_schema_version(
            database.handle,
            version,
            error)) {
        return false;
    }
    if (version != kSchemaVersion) {
        error =
            "Unsupported package-state schema version " +
            std::to_string(version) + ".";
        return false;
    }
    return true;
}

bool step_done(
    sqlite3 *database,
    sqlite3_stmt *statement,
    std::string &error)
{
    const int status = sqlite3_step(statement);
    if (status == SQLITE_DONE) {
        return true;
    }
    error = sqlite_error(database);
    return false;
}

bool reset_statement(
    sqlite3 *database,
    sqlite3_stmt *statement,
    std::string &error)
{
    if (sqlite3_reset(statement) != SQLITE_OK ||
        sqlite3_clear_bindings(statement) != SQLITE_OK) {
        error = sqlite_error(database);
        return false;
    }
    return true;
}

bool next_generation(
    sqlite3 *database,
    sqlite3_int64 &generation,
    std::string &error)
{
    Statement statement;
    if (!prepare(
            database,
            "SELECT COALESCE(MAX(id), 0) + 1"
            " FROM generations;",
            statement,
            error)) {
        return false;
    }
    if (sqlite3_step(statement.handle) != SQLITE_ROW) {
        error = sqlite_error(database);
        return false;
    }

    generation = sqlite3_column_int64(statement.handle, 0);
    return generation > 0;
}

bool insert_generation(
    sqlite3 *database,
    const sqlite3_int64 generation,
    const sqlite3_int64 published_at,
    const std::string_view fingerprint,
    std::string &error)
{
    Statement statement;
    if (!prepare(
            database,
            "INSERT INTO generations("
            " id, published_at, source_fingerprint"
            ") VALUES(?, ?, ?);",
            statement,
            error)) {
        return false;
    }

    if (sqlite3_bind_int64(
            statement.handle, 1, generation) != SQLITE_OK ||
        sqlite3_bind_int64(
            statement.handle, 2, published_at) != SQLITE_OK ||
        !bind_text(statement.handle, 3, fingerprint)) {
        error = sqlite_error(database);
        return false;
    }

    return step_done(database, statement.handle, error);
}

bool insert_installed(
    sqlite3 *database,
    const sqlite3_int64 generation,
    const std::vector<PackageRecord> &packages,
    std::string &error)
{
    Statement statement;
    if (!prepare(
            database,
            "INSERT INTO installed_packages("
            " generation_id, package_id, name, package_name,"
            " architecture, installed_version, available_version,"
            " installed_size_bytes, source, depends_text, pre_depends,"
            " provides, priority, multi_arch, essential"
            ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            statement,
            error)) {
        return false;
    }

    for (const PackageRecord &package : packages) {
        if (sqlite3_bind_int64(
                statement.handle, 1, generation) != SQLITE_OK ||
            !bind_text(statement.handle, 2, package.id) ||
            !bind_text(statement.handle, 3, package.name) ||
            !bind_text(statement.handle, 4, package.package_name) ||
            !bind_text(statement.handle, 5, package.architecture) ||
            !bind_text(statement.handle, 6, package.installed_version) ||
            !bind_text(statement.handle, 7, package.available_version) ||
            sqlite3_bind_int64(
                statement.handle,
                8,
                to_sqlite_integer(
                    package.installed_size_bytes)) != SQLITE_OK ||
            !bind_text(statement.handle, 9, package.source) ||
            !bind_text(statement.handle, 10, package.depends) ||
            !bind_text(statement.handle, 11, package.pre_depends) ||
            !bind_text(statement.handle, 12, package.provides) ||
            !bind_text(statement.handle, 13, package.priority) ||
            !bind_text(statement.handle, 14, package.multi_arch) ||
            sqlite3_bind_int(
                statement.handle, 15,
                package.essential ? 1 : 0) != SQLITE_OK) {
            error = sqlite_error(database);
            return false;
        }

        if (!step_done(database, statement.handle, error) ||
            !reset_statement(
                database, statement.handle, error)) {
            return false;
        }
    }

    return true;
}

bool insert_available(
    sqlite3 *database,
    const sqlite3_int64 generation,
    const std::vector<DebianPackageVersion> &packages,
    std::string &error)
{
    Statement statement;
    if (!prepare(
            database,
            "INSERT INTO available_packages("
            " generation_id, package_name, version, architecture,"
            " filename, sha256, source, priority, pin_priority,"
            " multi_arch, depends_text, pre_depends, recommends, provides,"
            " conflicts, breaks_text, replaces, description,"
            " size_bytes, installed_size_bytes, essential"
            ") VALUES("
            " ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,"
            " ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?"
            ");",
            statement,
            error)) {
        return false;
    }

    for (const DebianPackageVersion &package : packages) {
        if (sqlite3_bind_int64(
                statement.handle, 1, generation) != SQLITE_OK ||
            !bind_text(statement.handle, 2, package.package) ||
            !bind_text(statement.handle, 3, package.version) ||
            !bind_text(statement.handle, 4, package.architecture) ||
            !bind_text(statement.handle, 5, package.filename) ||
            !bind_text(statement.handle, 6, package.sha256) ||
            !bind_text(statement.handle, 7, package.source) ||
            !bind_text(statement.handle, 8, package.priority) ||
            sqlite3_bind_int(
                statement.handle, 9,
                package.pin_priority) != SQLITE_OK ||
            !bind_text(statement.handle, 10, package.multi_arch) ||
            !bind_text(statement.handle, 11, package.depends) ||
            !bind_text(statement.handle, 12, package.pre_depends) ||
            !bind_text(statement.handle, 13, package.recommends) ||
            !bind_text(statement.handle, 14, package.provides) ||
            !bind_text(statement.handle, 15, package.conflicts) ||
            !bind_text(statement.handle, 16, package.breaks) ||
            !bind_text(statement.handle, 17, package.replaces) ||
            !bind_text(statement.handle, 18, package.description) ||
            sqlite3_bind_int64(
                statement.handle,
                19,
                to_sqlite_integer(package.size_bytes)) != SQLITE_OK ||
            sqlite3_bind_int64(
                statement.handle,
                20,
                to_sqlite_integer(
                    package.installed_size_bytes)) != SQLITE_OK ||
            sqlite3_bind_int(
                statement.handle,
                21,
                package.essential ? 1 : 0) != SQLITE_OK) {
            error = sqlite_error(database);
            return false;
        }

        if (!step_done(database, statement.handle, error) ||
            !reset_statement(
                database, statement.handle, error)) {
            return false;
        }
    }

    return true;
}

bool publish_current(
    sqlite3 *database,
    const sqlite3_int64 generation,
    std::string &error)
{
    Statement statement;
    if (!prepare(
            database,
            "INSERT INTO current_state(singleton, generation_id)"
            " VALUES(1, ?)"
            " ON CONFLICT(singleton) DO UPDATE SET"
            " generation_id=excluded.generation_id;",
            statement,
            error)) {
        return false;
    }
    if (sqlite3_bind_int64(
            statement.handle, 1, generation) != SQLITE_OK) {
        error = sqlite_error(database);
        return false;
    }
    return step_done(database, statement.handle, error);
}

void rollback(sqlite3 *database) noexcept
{
    sqlite3_exec(
        database,
        "ROLLBACK;",
        nullptr,
        nullptr,
        nullptr);
}

} // namespace

PackageStateStore::PackageStateStore(std::string path)
    : path_(std::move(path))
{
}

const std::string &PackageStateStore::path() const noexcept
{
    return path_;
}

bool PackageStateStore::initialise(std::string &error) const
{
    Database database;
    return open_ready(path_, database, error);
}

bool PackageStateStore::publish(
    const std::vector<PackageRecord> &installed,
    const std::vector<DebianPackageVersion> &available,
    const std::string_view source_fingerprint,
    std::uint64_t &published_generation,
    std::string &error) const
{
    published_generation = 0U;
    Database database;
    if (!open_ready(path_, database, error)) {
        return false;
    }

    if (!exec_sql(database.handle, "BEGIN IMMEDIATE;", error)) {
        return false;
    }

    sqlite3_int64 generation = 0;
    const auto now =
        std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
    const sqlite3_int64 published_at =
        static_cast<sqlite3_int64>(now);

    if (!next_generation(
            database.handle, generation, error) ||
        !insert_generation(
            database.handle,
            generation,
            published_at,
            source_fingerprint,
            error) ||
        !insert_installed(
            database.handle,
            generation,
            installed,
            error) ||
        !insert_available(
            database.handle,
            generation,
            available,
            error) ||
        !publish_current(
            database.handle,
            generation,
            error) ||
        !exec_sql(
            database.handle,
            "DELETE FROM generations"
            " WHERE id NOT IN ("
            "  SELECT id FROM generations"
            "  ORDER BY id DESC LIMIT 2"
            " );",
            error) ||
        !exec_sql(database.handle, "COMMIT;", error)) {
        rollback(database.handle);
        return false;
    }

    published_generation =
        static_cast<std::uint64_t>(generation);
    return true;
}

std::optional<PackageStateSnapshot>
PackageStateStore::load_current(
    std::string &error) const
{
    Database database;
    if (!open_reader(path_, database, error)) {
        return std::nullopt;
    }

    PackageStateSnapshot snapshot;

    {
        Statement statement;
        if (!prepare(
                database.handle,
                "SELECT g.id, g.published_at, g.source_fingerprint"
                " FROM current_state c"
                " JOIN generations g ON g.id=c.generation_id"
                " WHERE c.singleton=1;",
                statement,
                error)) {
            return std::nullopt;
        }

        const int status = sqlite3_step(statement.handle);
        if (status == SQLITE_DONE) {
            error.clear();
            return std::nullopt;
        }
        if (status != SQLITE_ROW) {
            error = sqlite_error(database.handle);
            return std::nullopt;
        }

        snapshot.generation =
            from_sqlite_unsigned(
                sqlite3_column_int64(statement.handle, 0));
        snapshot.published_at_unix =
            sqlite3_column_int64(statement.handle, 1);
        snapshot.source_fingerprint =
            column_text(statement.handle, 2);
    }

    {
        Statement statement;
        if (!prepare(
                database.handle,
                "SELECT package_id, name, package_name,"
                " architecture, installed_version,"
                " available_version, installed_size_bytes, source,"
                " depends_text, pre_depends, provides, priority,"
                " multi_arch, essential"
                " FROM installed_packages"
                " WHERE generation_id=?"
                " ORDER BY package_id;",
                statement,
                error)) {
            return std::nullopt;
        }
        if (sqlite3_bind_int64(
                statement.handle,
                1,
                static_cast<sqlite3_int64>(
                    snapshot.generation)) != SQLITE_OK) {
            error = sqlite_error(database.handle);
            return std::nullopt;
        }

        for (;;) {
            const int status = sqlite3_step(statement.handle);
            if (status == SQLITE_DONE) {
                break;
            }
            if (status != SQLITE_ROW) {
                error = sqlite_error(database.handle);
                return std::nullopt;
            }

            PackageRecord package;
            package.id = column_text(statement.handle, 0);
            package.name = column_text(statement.handle, 1);
            package.package_name =
                column_text(statement.handle, 2);
            package.architecture =
                column_text(statement.handle, 3);
            package.installed_version =
                column_text(statement.handle, 4);
            package.available_version =
                column_text(statement.handle, 5);
            package.installed_size_bytes =
                from_sqlite_unsigned(
                    sqlite3_column_int64(
                        statement.handle, 6));
            package.source =
                column_text(statement.handle, 7);
            package.depends =
                column_text(statement.handle, 8);
            package.pre_depends =
                column_text(statement.handle, 9);
            package.provides =
                column_text(statement.handle, 10);
            package.priority =
                column_text(statement.handle, 11);
            package.multi_arch =
                column_text(statement.handle, 12);
            package.essential =
                sqlite3_column_int(statement.handle, 13) != 0;
            package.state = InstallState::installed;
            snapshot.installed.emplace_back(std::move(package));
        }
    }

    {
        Statement statement;
        if (!prepare(
                database.handle,
                "SELECT package_name, version, architecture,"
                " filename, sha256, source, priority, pin_priority,"
                " multi_arch, depends_text, pre_depends, recommends, provides,"
                " conflicts, breaks_text, replaces, description,"
                " size_bytes, installed_size_bytes, essential"
                " FROM available_packages"
                " WHERE generation_id=?"
                " ORDER BY package_name, architecture, version, source;",
                statement,
                error)) {
            return std::nullopt;
        }
        if (sqlite3_bind_int64(
                statement.handle,
                1,
                static_cast<sqlite3_int64>(
                    snapshot.generation)) != SQLITE_OK) {
            error = sqlite_error(database.handle);
            return std::nullopt;
        }

        for (;;) {
            const int status = sqlite3_step(statement.handle);
            if (status == SQLITE_DONE) {
                break;
            }
            if (status != SQLITE_ROW) {
                error = sqlite_error(database.handle);
                return std::nullopt;
            }

            DebianPackageVersion package;
            package.package = column_text(statement.handle, 0);
            package.version = column_text(statement.handle, 1);
            package.architecture = column_text(statement.handle, 2);
            package.filename = column_text(statement.handle, 3);
            package.sha256 = column_text(statement.handle, 4);
            package.source = column_text(statement.handle, 5);
            package.priority = column_text(statement.handle, 6);
            package.pin_priority =
                sqlite3_column_int(statement.handle, 7);
            package.multi_arch = column_text(statement.handle, 8);
            package.depends = column_text(statement.handle, 9);
            package.pre_depends = column_text(statement.handle, 10);
            package.recommends = column_text(statement.handle, 11);
            package.provides = column_text(statement.handle, 12);
            package.conflicts = column_text(statement.handle, 13);
            package.breaks = column_text(statement.handle, 14);
            package.replaces = column_text(statement.handle, 15);
            package.description = column_text(statement.handle, 16);
            package.size_bytes =
                from_sqlite_unsigned(
                    sqlite3_column_int64(
                        statement.handle, 17));
            package.installed_size_bytes =
                from_sqlite_unsigned(
                    sqlite3_column_int64(
                        statement.handle, 18));
            package.essential =
                sqlite3_column_int(
                    statement.handle, 19) != 0;
            snapshot.available.emplace_back(std::move(package));
        }
    }

    error.clear();
    return snapshot;
}

} // namespace infiltrator::software
