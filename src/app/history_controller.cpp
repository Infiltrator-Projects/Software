// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/history_controller.hpp"

#include "app/history_view.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

struct HistoryResult {
    unsigned int generation{0U};
    std::vector<TransactionHistoryItem> records;
    std::string error;
};

struct HistoryTaskData {
    unsigned int generation{0U};
};

std::string one_line(std::string value)
{
    for (char &ch : value) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    while (!value.empty() &&
           std::isspace(
               static_cast<unsigned char>(
                   value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

void history_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<HistoryTaskData *>(task_data);
    auto *result = new HistoryResult{};
    result->generation =
        data == nullptr ? 0U : data->generation;

    const std::string user_path =
        user_transaction_history_path();
    if (user_path.empty()) {
        result->error =
            "The user data directory is unavailable.";
    } else {
        TransactionHistoryStore store(user_path);
        std::string user_error;
        result->records =
            store.load_recent(100U, user_error);
        if (!user_error.empty()) {
            result->error =
                "User history: " + user_error;
        }

        const std::string system_path =
            system_transaction_history_path();
        if (std::filesystem::exists(system_path)) {
            TransactionHistoryStore system_store(
                system_path);
            std::string system_error;
            std::vector<TransactionHistoryItem> system_records =
                system_store.load_recent(
                    100U,
                    system_error);
            if (system_error.empty()) {
                for (TransactionHistoryItem &entry :
                     system_records) {
                    /*
                     * User and system SQLite databases each allocate IDs from
                     * one. Negative IDs namespace root-owned transactions so
                     * the view never groups unrelated records together.
                     */
                    entry.transaction_id =
                        -entry.transaction_id;
                }
                result->records.insert(
                    result->records.end(),
                    std::make_move_iterator(
                        system_records.begin()),
                    std::make_move_iterator(
                        system_records.end()));
            } else {
                if (!result->error.empty()) {
                    result->error += " ";
                }
                result->error +=
                    "System history: " +
                    system_error;
            }
        }

        std::stable_sort(
            result->records.begin(),
            result->records.end(),
            [](const TransactionHistoryItem &left,
               const TransactionHistoryItem &right) {
                if (left.completed_at_unix !=
                    right.completed_at_unix) {
                    return left.completed_at_unix >
                        right.completed_at_unix;
                }
                if (left.transaction_id !=
                    right.transaction_id) {
                    return left.transaction_id >
                        right.transaction_id;
                }
                return false;
            });
        if (result->records.size() > 200U) {
            result->records.resize(200U);
        }
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<HistoryResult *>(pointer);
        });
}

void history_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *controller =
        static_cast<HistoryController *>(user_data);
    auto *result =
        static_cast<HistoryResult *>(
            g_task_propagate_pointer(
                G_TASK(async_result), nullptr));

    if (controller == nullptr || result == nullptr) {
        delete result;
        return;
    }
    if (source_object != G_OBJECT(controller->window) ||
        result->generation != controller->generation) {
        delete result;
        return;
    }

    controller->busy = false;
    controller->records =
        std::move(result->records);
    const std::string error = result->error;
    delete result;

    rebuild_history_view(
        controller->list,
        controller->count,
        controller->records);

    if (controller->changed != nullptr) {
        controller->changed(
            controller->changed_data);
    }

    if (controller->status != nullptr) {
        if (!error.empty()) {
            const std::string message =
                "Unable to read transaction history: " +
                one_line(error);
            gtk_label_set_text(
                GTK_LABEL(controller->status),
                message.c_str());
        } else if (controller->records.empty()) {
            gtk_label_set_text(
                GTK_LABEL(controller->status),
                "No completed software transactions have been recorded yet.");
        } else {
            std::unordered_set<std::int64_t> transactions;
            for (const TransactionHistoryItem &entry :
                 controller->records) {
                transactions.insert(
                    entry.transaction_id);
            }
            const std::string message =
                std::to_string(transactions.size()) +
                (transactions.size() == 1U
                     ? " recent transaction loaded."
                     : " recent transactions loaded.");
            gtk_label_set_text(
                GTK_LABEL(controller->status),
                message.c_str());
        }
    }

    if (controller->refresh != nullptr) {
        gtk_widget_set_sensitive(
            controller->refresh, true);
    }
}

void history_refresh_clicked(
    GtkButton *,
    gpointer user_data)
{
    refresh_history_controller(
        static_cast<HistoryController *>(user_data));
}

} // namespace

GtkWidget *create_history_controller_page(
    HistoryController *controller,
    GtkWindow *window,
    void (*changed)(gpointer),
    gpointer changed_data)
{
    if (controller == nullptr) {
        return gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 0);
    }

    controller->window = window;
    controller->changed = changed;
    controller->changed_data = changed_data;

    return create_history_page(
        &controller->list,
        &controller->status,
        &controller->count,
        &controller->refresh,
        G_CALLBACK(history_refresh_clicked),
        controller);
}

void refresh_history_controller(
    HistoryController *controller)
{
    if (controller == nullptr ||
        controller->window == nullptr ||
        controller->list == nullptr ||
        controller->busy) {
        return;
    }

    controller->loaded = true;
    controller->busy = true;
    ++controller->generation;

    if (controller->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(controller->status),
            "Reading durable transaction history…");
    }
    if (controller->refresh != nullptr) {
        gtk_widget_set_sensitive(
            controller->refresh, false);
    }

    auto *data = new HistoryTaskData{
        controller->generation};
    GTask *task =
        g_task_new(
            G_OBJECT(controller->window),
            nullptr,
            history_complete,
            controller);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<HistoryTaskData *>(pointer);
        });
    g_task_run_in_thread(
        task, history_worker);
    g_object_unref(task);
}

} // namespace infiltrator::software
