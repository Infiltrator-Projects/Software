// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/transaction_history.hpp"

#include <cassert>
#include <filesystem>
#include <string>

int main()
{
    using namespace infiltrator::software;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "infiltrator-software-history-test.sqlite3";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    TransactionPlan plan;
    plan.source_fingerprint = "fixture-fingerprint";

    TransactionItem first;
    first.package_id = "alpha";
    first.action = TransactionAction::upgrade;
    first.from_version = "1.0";
    first.to_version = "1.1";
    first.source = "fixture";
    first.requested = true;
    first.system_critical = false;
    plan.items.push_back(first);

    TransactionItem second;
    second.package_id = "libbeta";
    second.action = TransactionAction::upgrade;
    second.from_version = "2.0";
    second.to_version = "2.1";
    second.source = "fixture";
    second.requested = false;
    second.system_critical = true;
    plan.items.push_back(second);

    TransactionHistoryStore store(path.string());
    std::string error;
    assert(store.append(plan, true, "Completed.", error));
    assert(error.empty());

    const auto rows = store.load_recent(20U, error);
    assert(error.empty());
    assert(rows.size() == 2U);
    assert(rows[0].transaction_id == rows[1].transaction_id);
    assert(rows[0].success);
    assert(rows[0].package_id == "alpha");
    assert(rows[0].from_version == "1.0");
    assert(rows[0].to_version == "1.1");
    assert(rows[0].requested);
    assert(rows[1].package_id == "libbeta");
    assert(rows[1].system_critical);

    assert(store.append(plan, false, "Failed.", error));
    const auto newest = store.load_recent(1U, error);
    assert(error.empty());
    assert(newest.size() == 2U);
    assert(!newest[0].success);
    assert(newest[0].message == "Failed.");

    std::filesystem::remove(path, ec);
    return 0;
}
