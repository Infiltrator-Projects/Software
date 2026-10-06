// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/repository_controller.hpp"

#include "sources/mirror_health.hpp"
#include "sources/source_inventory.hpp"

#include <sstream>
#include <string>
#include <utility>

#ifndef INFILTRATOR_SOFTWARE_VERSION
#define INFILTRATOR_SOFTWARE_VERSION "0.0.0"
#endif

namespace infiltrator::software {
namespace {

struct RepositoryResult {
    unsigned int generation{0U};
    std::vector<SourceRecord> sources;
    std::string mirror_status;
    std::string error;
};

struct RepositoryTaskData {
    unsigned int generation{0U};
};

std::string mint_mirror_status()
{
    return sources::mint_mirror_status(
        "Infiltrator-Software/" INFILTRATOR_SOFTWARE_VERSION);
}

void repositories_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data = static_cast<RepositoryTaskData *>(task_data);
    auto *result = new RepositoryResult{};
    result->generation = data == nullptr ? 0U : data->generation;

    SourceInventory inventory;
    result->sources = inventory.list(result->error);
    if (result->error.empty()) {
        result->mirror_status = mint_mirror_status();
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<RepositoryResult *>(pointer);
        });
}

void repositories_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *controller =
        static_cast<RepositoryController *>(user_data);
    auto *result =
        static_cast<RepositoryResult *>(
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
    controller->records = std::move(result->sources);

    if (controller->flow != nullptr) {
        GtkWidget *child =
            gtk_widget_get_first_child(controller->flow);
        while (child != nullptr) {
            GtkWidget *next =
                gtk_widget_get_next_sibling(child);
            gtk_flow_box_remove(
                GTK_FLOW_BOX(controller->flow), child);
            child = next;
        }

        if (controller->make_card != nullptr) {
            for (const SourceRecord &source : controller->records) {
                GtkWidget *card =
                    controller->make_card(
                        controller->callback_data,
                        source);
                if (card != nullptr) {
                    gtk_flow_box_append(
                        GTK_FLOW_BOX(controller->flow),
                        card);
                }
            }
        }
    }

    std::size_t enabled = 0U;
    for (const SourceRecord &source : controller->records) {
        if (source.enabled) ++enabled;
    }

    if (controller->count != nullptr) {
        const std::string count =
            std::to_string(controller->records.size());
        gtk_label_set_text(
            GTK_LABEL(controller->count),
            count.c_str());
    }

    if (controller->status != nullptr) {
        if (!result->error.empty()) {
            gtk_label_set_text(
                GTK_LABEL(controller->status),
                result->error.c_str());
        } else {
            std::ostringstream status;
            status << enabled << " enabled source"
                   << (enabled == 1U ? "" : "s")
                   << " detected. APT sources and Flatpak remotes feed Discover.";
            if (!result->mirror_status.empty()) {
                status << "  " << result->mirror_status;
            }
            gtk_label_set_text(
                GTK_LABEL(controller->status),
                status.str().c_str());
        }
    }

    if (controller->changed != nullptr) {
        controller->changed(controller->callback_data);
    }

    delete result;
}

} // namespace

void configure_repository_controller(
    RepositoryController *controller,
    GtkWindow *window,
    GtkWidget *flow,
    GtkWidget *count,
    GtkWidget *status,
    GtkWidget *(*make_card)(gpointer, const SourceRecord &),
    void (*changed)(gpointer),
    gpointer callback_data)
{
    if (controller == nullptr) return;
    controller->window = window;
    controller->flow = flow;
    controller->count = count;
    controller->status = status;
    controller->make_card = make_card;
    controller->changed = changed;
    controller->callback_data = callback_data;
}

void refresh_repository_controller(
    RepositoryController *controller)
{
    if (controller == nullptr ||
        controller->window == nullptr ||
        controller->flow == nullptr ||
        controller->busy) {
        return;
    }

    controller->loaded = true;
    controller->busy = true;
    ++controller->generation;

    if (controller->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(controller->status),
            "Reading configured software sources…");
    }

    auto *data = new RepositoryTaskData{
        controller->generation};
    GTask *task =
        g_task_new(
            G_OBJECT(controller->window),
            nullptr,
            repositories_complete,
            controller);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<RepositoryTaskData *>(pointer);
        });
    g_task_run_in_thread(task, repositories_worker);
    g_object_unref(task);
}

} // namespace infiltrator::software
