// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/transaction_history.hpp"

#include <chrono>
#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

class Database final {
public:
    ~Database()
    {
        if (handle != nullptr) sqlite3_close(handle);
    }

    sqlite3 *handle{};
};

class Statement final {
public:
    ~Statement()
    {
        if (handle != nullptr) sqlite3_finalize(handle);
    }

    sqlite3_stmt *handle{};
};

std::string sqlite_error(sqlite3 *database)
{
    return database == nullptr
        ? "SQLite database is unavailable."
        : sqlite3_errmsg(database);
}

bool exec_sql(
    sqlite3 *database,
    const char *sql,
    std::string &error)
{
    char *message = nullptr;
    const int status =
        sqlite3_exec(database, sql, nullptr, nullptr, &message);
    if (status == SQLITE_OK) return true;

    error =
        message == nullptr
            ? sqlite_error(database)
            : std::string(message);
    sqlite3_free(message);
    return false;
}

bool open_database(
    const std::string &path,
    Database &database,
    std::string &error)
{
    std::error_code ec;
    const std::filesystem::path fs_path(path);
    const auto parent = fs_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error =
                "Unable to create history directory: " +
                ec.message();
            return false;
        }
    }

    if (sqlite3_open_v2(
            path.c_str(),
            &database.handle,
            SQLITE_OPEN_READWRITE |
                SQLITE_OPEN_CREATE |
                SQLITE_OPEN_FULLMUTEX,
            nullptr) != SQLITE_OK) {
        error = sqlite_error(database.handle);
        return false;
    }

    (void)sqlite3_busy_timeout(database.handle, 5000);
    return true;
}

bool ensure_schema(sqlite3 *database, std::string &error)
{
    static constexpr const char *schema =
        "PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS transactions ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " completed_at INTEGER NOT NULL,"
        " action TEXT NOT NULL,"
        " success INTEGER NOT NULL,"
        " message TEXT NOT NULL,"
        " source_fingerprint TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS transaction_items ("
        " transaction_id INTEGER NOT NULL"
        "   REFERENCES transactions(id) ON DELETE CASCADE,"
        " ordinal INTEGER NOT NULL,"
        " package_id TEXT NOT NULL,"
        " action TEXT NOT NULL,"
        " from_version TEXT NOT NULL,"
        " to_version TEXT NOT NULL,"
        " source TEXT NOT NULL,"
        " requested INTEGER NOT NULL,"
        " system_critical INTEGER NOT NULL,"
        " PRIMARY KEY(transaction_id, ordinal)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_history_completed"
        " ON transactions(completed_at DESC, id DESC);";
    return exec_sql(database, schema, error);
}

std::string action_text(const TransactionAction action)
{
    switch (action) {
    case TransactionAction::install: return "install";
    case TransactionAction::upgrade: return "upgrade";
    case TransactionAction::remove: return "remove";
    }
    return "install";
}

TransactionAction parse_action(const std::string_view value)
{
    if (value == "upgrade") return TransactionAction::upgrade;
    if (value == "remove") return TransactionAction::remove;
    return TransactionAction::install;
}

bool bind_text(
    sqlite3_stmt *statement,
    const int index,
    const std::string_view value)
{
    return sqlite3_bind_text(
               statement,
               index,
               value.data(),
               static_cast<int>(value.size()),
               SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string column_text(
    sqlite3_stmt *statement,
    const int column)
{
    const unsigned char *value =
        sqlite3_column_text(statement, column);
    return value == nullptr
        ? std::string{}
        : std::string(
              reinterpret_cast<const char *>(value));
}

} // namespace

TransactionHistoryStore::TransactionHistoryStore(
    std::string path)
    : path_(std::move(path))
{
}

const std::string &
TransactionHistoryStore::path() const noexcept
{
    return path_;
}

bool TransactionHistoryStore::append(
    const TransactionPlan &plan,
    const bool success,
    const std::string_view message,
    std::string &error) const
{
    error.clear();
    if (plan.items.empty()) {
        error = "Cannot record an empty transaction.";
        return false;
    }

    Database database;
    if (!open_database(path_, database, error) ||
        !ensure_schema(database.handle, error) ||
        !exec_sql(database.handle, "BEGIN IMMEDIATE;", error)) {
        return false;
    }

    const auto rollback = [&]() {
        std::string ignored;
        (void)exec_sql(
            database.handle, "ROLLBACK;", ignored);
    };

    Statement transaction;
    if (sqlite3_prepare_v2(
            database.handle,
            "INSERT INTO transactions("
            " completed_at, action, success, message, source_fingerprint"
            ") VALUES(?, ?, ?, ?, ?);",
            -1,
            &transaction.handle,
            nullptr) != SQLITE_OK) {
        error = sqlite_error(database.handle);
        rollback();
        return false;
    }

    const auto completed =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now()
                .time_since_epoch())
            .count();
    const std::string top_action =
        action_text(plan.items.front().action);

    if (sqlite3_bind_int64(
            transaction.handle, 1,
            static_cast<sqlite3_int64>(completed)) != SQLITE_OK ||
        !bind_text(transaction.handle, 2, top_action) ||
        sqlite3_bind_int(
            transaction.handle, 3,
            success ? 1 : 0) != SQLITE_OK ||
        !bind_text(transaction.handle, 4, message) ||
        !bind_text(
            transaction.handle, 5,
            plan.source_fingerprint) ||
        sqlite3_step(transaction.handle) != SQLITE_DONE) {
        error = sqlite_error(database.handle);
        rollback();
        return false;
    }

    const sqlite3_int64 transaction_id =
        sqlite3_last_insert_rowid(database.handle);

    Statement item;
    if (sqlite3_prepare_v2(
            database.handle,
            "INSERT INTO transaction_items("
            " transaction_id, ordinal, package_id, action,"
            " from_version, to_version, source, requested,"
            " system_critical"
            ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);",
            -1,
            &item.handle,
            nullptr) != SQLITE_OK) {
        error = sqlite_error(database.handle);
        rollback();
        return false;
    }

    int ordinal = 0;
    for (const TransactionItem &entry : plan.items) {
        if (sqlite3_reset(item.handle) != SQLITE_OK ||
            sqlite3_clear_bindings(item.handle) != SQLITE_OK ||
            sqlite3_bind_int64(
                item.handle, 1, transaction_id) != SQLITE_OK ||
            sqlite3_bind_int(
                item.handle, 2, ordinal++) != SQLITE_OK ||
            !bind_text(item.handle, 3, entry.package_id) ||
            !bind_text(
                item.handle, 4,
                action_text(entry.action)) ||
            !bind_text(
                item.handle, 5, entry.from_version) ||
            !bind_text(
                item.handle, 6, entry.to_version) ||
            !bind_text(item.handle, 7, entry.source) ||
            sqlite3_bind_int(
                item.handle, 8,
                entry.requested ? 1 : 0) != SQLITE_OK ||
            sqlite3_bind_int(
                item.handle, 9,
                entry.system_critical ? 1 : 0) != SQLITE_OK ||
            sqlite3_step(item.handle) != SQLITE_DONE) {
            error = sqlite_error(database.handle);
            rollback();
            return false;
        }
    }

    if (!exec_sql(database.handle, "COMMIT;", error)) {
        rollback();
        return false;
    }

    return true;
}

std::vector<TransactionHistoryItem>
TransactionHistoryStore::load_recent(
    const std::size_t limit,
    std::string &error) const
{
    error.clear();
    std::vector<TransactionHistoryItem> result;

    Database database;
    if (!open_database(path_, database, error) ||
        !ensure_schema(database.handle, error)) {
        return result;
    }

    Statement statement;
    if (sqlite3_prepare_v2(
            database.handle,
            "SELECT t.id, t.completed_at, t.action, t.success,"
            " t.message, i.package_id, i.action,"
            " i.from_version, i.to_version, i.source,"
            " i.requested, i.system_critical"
            " FROM transactions AS t"
            " JOIN transaction_items AS i"
            "   ON i.transaction_id=t.id"
            " WHERE t.id IN ("
            "   SELECT id FROM transactions"
            "   ORDER BY completed_at DESC, id DESC LIMIT ?"
            " )"
            " ORDER BY t.completed_at DESC, t.id DESC, i.ordinal ASC;",
            -1,
            &statement.handle,
            nullptr) != SQLITE_OK) {
        error = sqlite_error(database.handle);
        return {};
    }

    const sqlite3_int64 bounded =
        static_cast<sqlite3_int64>(
            limit > 1000U ? 1000U : limit);
    if (sqlite3_bind_int64(
            statement.handle, 1, bounded) != SQLITE_OK) {
        error = sqlite_error(database.handle);
        return {};
    }

    while (sqlite3_step(statement.handle) == SQLITE_ROW) {
        TransactionHistoryItem entry;
        entry.transaction_id =
            sqlite3_column_int64(statement.handle, 0);
        entry.completed_at_unix =
            sqlite3_column_int64(statement.handle, 1);
        entry.action =
            parse_action(column_text(statement.handle, 6));
        entry.success =
            sqlite3_column_int(statement.handle, 3) != 0;
        entry.message = column_text(statement.handle, 4);
        entry.package_id = column_text(statement.handle, 5);
        entry.from_version = column_text(statement.handle, 7);
        entry.to_version = column_text(statement.handle, 8);
        entry.source = column_text(statement.handle, 9);
        entry.requested =
            sqlite3_column_int(statement.handle, 10) != 0;
        entry.system_critical =
            sqlite3_column_int(statement.handle, 11) != 0;
        result.emplace_back(std::move(entry));
    }

    if (sqlite3_errcode(database.handle) != SQLITE_DONE &&
        sqlite3_errcode(database.handle) != SQLITE_OK) {
        error = sqlite_error(database.handle);
        return {};
    }

    return result;
}

} // namespace infiltrator::software
