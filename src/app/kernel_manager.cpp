// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/kernel_manager.hpp"

#include "client/engine_client.hpp"
#include "core/exact_transaction_spec.hpp"
#include "core/transaction_history.hpp"
#include "engine/kernel_inventory.hpp"

#include <gio/gio.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

constexpr const char *kKernelTypes[] = {
    "-generic",
    "-lowlatency",
    "-aws",
    "-azure",
    "-gcp",
    "-kvm",
    "-oem",
    "-oracle",
    nullptr
};

struct QueuedKernel {
    KernelRecord kernel;
    bool install{false};
};

struct KernelManagerContext {
    GtkWindow *window{};
    GtkWindow *parent{};
    GtkWidget *status{};
    GtkWidget *current{};
    GtkWidget *type_dropdown{};
    GtkListBox *series_list{};
    GtkListBox *kernel_list{};
    GtkListBox *queue_list{};
    GtkWidget *apply_queue{};
    GtkWidget *bulk_remove{};
    std::vector<KernelRecord> kernels;
    std::vector<QueuedKernel> queued;
    std::string selected_series;
    unsigned generation{0U};
    bool busy{false};
    KernelManagerChangedCallback changed_callback{};
    gpointer changed_user_data{};
};

struct LoadTaskData {
    unsigned generation{0U};
    std::string kernel_type;
    bool refresh_installed{false};
};

struct LoadResult {
    unsigned generation{0U};
    std::vector<KernelRecord> kernels;
    std::string error;
};

struct PlanTaskData {
    std::vector<std::string> install_ids;
    std::vector<std::string> remove_ids;
};

struct PlanResult {
    std::optional<TransactionPlan> plan;
    std::string error;
};

struct ApplyOperation {
    GtkWindow *window{};
    KernelManagerContext *context{};
    TransactionPlan plan;
};

std::string kernel_key(
    const KernelRecord &kernel)
{
    return kernel.version + "|" + kernel.kernel_type;
}

std::filesystem::path history_path()
{
    const char *data = g_get_user_data_dir();
    if (data == nullptr || *data == '\0') return {};
    return std::filesystem::path(data) /
        "infiltrator-software" / "history.sqlite3";
}

void record_history(
    const TransactionPlan &plan,
    const bool success,
    const std::string_view message)
{
    const std::filesystem::path path = history_path();
    if (path.empty() || plan.items.empty()) return;

    TransactionHistoryStore store(path.string());
    std::string error;
    if (!store.append(plan, success, message, error)) {
        g_warning(
            "Unable to record kernel transaction history: %s",
            error.c_str());
    }
}

void clear_list(GtkListBox *list)
{
    if (list == nullptr) return;
    GtkWidget *child =
        gtk_widget_get_first_child(GTK_WIDGET(list));
    while (child != nullptr) {
        GtkWidget *next =
            gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(list, child);
        child = next;
    }
}

GtkWidget *label(
    const std::string &text,
    const char *css_class = nullptr,
    const float xalign = 0.0F)
{
    GtkWidget *widget = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(widget), xalign);
    if (css_class != nullptr) {
        gtk_widget_add_css_class(widget, css_class);
    }
    return widget;
}

std::string selected_kernel_type(
    KernelManagerContext *context)
{
    if (context == nullptr ||
        context->type_dropdown == nullptr) {
        return "-generic";
    }
    const guint selected =
        gtk_drop_down_get_selected(
            GTK_DROP_DOWN(context->type_dropdown));
    if (selected >= 8U) return "-generic";
    return kKernelTypes[selected];
}

void rebuild_kernel_rows(KernelManagerContext *context);
void rebuild_queue(KernelManagerContext *context);
void begin_kernel_actions(
    KernelManagerContext *context,
    const std::vector<QueuedKernel> &actions);
void refresh_kernels(
    KernelManagerContext *context,
    bool refresh_installed);

void set_status(
    KernelManagerContext *context,
    const std::string &text)
{
    if (context != nullptr &&
        context->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(context->status),
            text.c_str());
    }
}

void queue_action(
    KernelManagerContext *context,
    const KernelRecord &kernel,
    const bool install)
{
    if (context == nullptr) return;

    const std::string key = kernel_key(kernel);
    auto existing =
        std::find_if(
            context->queued.begin(),
            context->queued.end(),
            [&](const QueuedKernel &item) {
                return kernel_key(item.kernel) == key;
            });
    if (existing == context->queued.end()) {
        context->queued.push_back(
            QueuedKernel{kernel, install});
    } else {
        existing->kernel = kernel;
        existing->install = install;
    }
    rebuild_queue(context);
}

void unqueue_action(
    KernelManagerContext *context,
    const std::string &key)
{
    if (context == nullptr) return;
    context->queued.erase(
        std::remove_if(
            context->queued.begin(),
            context->queued.end(),
            [&](const QueuedKernel &item) {
                return kernel_key(item.kernel) == key;
            }),
        context->queued.end());
    rebuild_queue(context);
}

void queue_remove_clicked(
    GtkButton *button,
    gpointer user_data)
{
    auto *context =
        static_cast<KernelManagerContext *>(user_data);
    auto *kernel =
        static_cast<KernelRecord *>(
            g_object_get_data(
                G_OBJECT(button),
                "kernel-record"));
    if (context == nullptr || kernel == nullptr) return;
    queue_action(context, *kernel, false);
}

void queue_install_clicked(
    GtkButton *button,
    gpointer user_data)
{
    auto *context =
        static_cast<KernelManagerContext *>(user_data);
    auto *kernel =
        static_cast<KernelRecord *>(
            g_object_get_data(
                G_OBJECT(button),
                "kernel-record"));
    if (context == nullptr || kernel == nullptr) return;
    queue_action(context, *kernel, true);
}

void install_now_clicked(
    GtkButton *button,
    gpointer user_data)
{
    auto *context =
        static_cast<KernelManagerContext *>(user_data);
    auto *kernel =
        static_cast<KernelRecord *>(
            g_object_get_data(
                G_OBJECT(button),
                "kernel-record"));
    if (context == nullptr || kernel == nullptr) return;
    begin_kernel_actions(
        context,
        std::vector<QueuedKernel>{
            QueuedKernel{*kernel, true}});
}

void remove_now_clicked(
    GtkButton *button,
    gpointer user_data)
{
    auto *context =
        static_cast<KernelManagerContext *>(user_data);
    auto *kernel =
        static_cast<KernelRecord *>(
            g_object_get_data(
                G_OBJECT(button),
                "kernel-record"));
    if (context == nullptr || kernel == nullptr) return;
    begin_kernel_actions(
        context,
        std::vector<QueuedKernel>{
            QueuedKernel{*kernel, false}});
}

void queued_remove_clicked(
    GtkButton *button,
    gpointer user_data)
{
    auto *context =
        static_cast<KernelManagerContext *>(user_data);
    const auto *key =
        static_cast<const std::string *>(
            g_object_get_data(
                G_OBJECT(button),
                "kernel-queue-key"));
    if (context == nullptr || key == nullptr) return;
    unqueue_action(context, *key);
}

void rebuild_queue(KernelManagerContext *context)
{
    if (context == nullptr ||
        context->queue_list == nullptr) {
        return;
    }
    clear_list(context->queue_list);

    for (const QueuedKernel &queued :
         context->queued) {
        GtkWidget *row =
            gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);
        gtk_widget_set_margin_top(row, 6);
        gtk_widget_set_margin_bottom(row, 6);

        GtkWidget *copy =
            label(
                (queued.install ? "Install " : "Remove ") +
                queued.kernel.version +
                queued.kernel.kernel_type);
        gtk_widget_set_hexpand(copy, true);
        gtk_box_append(GTK_BOX(row), copy);

        GtkWidget *remove =
            gtk_button_new_with_label("Remove from queue");
        auto *key =
            new std::string(kernel_key(queued.kernel));
        g_object_set_data_full(
            G_OBJECT(remove),
            "kernel-queue-key",
            key,
            [](gpointer pointer) {
                delete static_cast<std::string *>(pointer);
            });
        g_signal_connect(
            remove,
            "clicked",
            G_CALLBACK(queued_remove_clicked),
            context);
        gtk_box_append(GTK_BOX(row), remove);

        GtkWidget *box_row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(box_row), row);
        gtk_list_box_append(
            context->queue_list, box_row);
    }

    if (context->apply_queue != nullptr) {
        gtk_widget_set_sensitive(
            context->apply_queue,
            !context->queued.empty() &&
                !context->busy);
        std::string text =
            context->queued.empty()
                ? "Perform queued actions"
                : "Perform queued actions (" +
                    std::to_string(
                        context->queued.size()) +
                    ")";
        gtk_button_set_label(
            GTK_BUTTON(context->apply_queue),
            text.c_str());
    }
}

std::string support_copy(
    const KernelRecord &kernel)
{
    if (!kernel.support_status.empty()) {
        return kernel.support_status;
    }
    return "Support status unavailable";
}

GtkWidget *kernel_links(
    const KernelRecord &kernel)
{
    GtkWidget *box =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    if (kernel.origin == "Ubuntu" ||
        kernel.archive.find("ubuntu") !=
            std::string::npos ||
        kernel.archive.find("noble") !=
            std::string::npos) {
        const std::string bugs =
            "https://launchpad.net/ubuntu/+source/linux/+bugs?field.searchtext=" +
            kernel.version;
        GtkWidget *bug =
            gtk_link_button_new_with_label(
                bugs.c_str(), "Bug reports");
        gtk_box_append(GTK_BOX(box), bug);

        if (!kernel.package_version.empty()) {
            std::string package_version =
                kernel.package_version;
            const std::size_t tilde =
                package_version.find('~');
            if (tilde != std::string::npos) {
                package_version.erase(tilde);
            }
            const std::string changelog =
                "https://changelogs.ubuntu.com/changelogs/pool/main/l/linux/linux_" +
                package_version + "/changelog";
            GtkWidget *change =
                gtk_link_button_new_with_label(
                    changelog.c_str(), "Changelog");
            gtk_box_append(GTK_BOX(box), change);
        }

        GtkWidget *cve =
            gtk_link_button_new_with_label(
                "https://ubuntu.com/security/cves?package=linux",
                "CVE tracker");
        gtk_box_append(GTK_BOX(box), cve);
    }
    return box;
}

GtkWidget *make_kernel_row(
    KernelManagerContext *context,
    const KernelRecord &kernel)
{
    GtkWidget *outer =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(outer, 12);
    gtk_widget_set_margin_end(outer, 12);
    gtk_widget_set_margin_top(outer, 10);
    gtk_widget_set_margin_bottom(outer, 10);

    GtkWidget *top =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *version =
        label(
            kernel.version +
            (kernel.kernel_type == selected_kernel_type(context)
                 ? std::string{}
                 : kernel.kernel_type));
    gtk_widget_add_css_class(version, "card-title");
    gtk_widget_set_hexpand(version, true);
    gtk_box_append(GTK_BOX(top), version);

    std::string state;
    if (kernel.active) {
        state = "Active";
    } else if (kernel.installed) {
        state = "Installed";
    } else if (kernel.installable) {
        state = "Available";
    }
    if (!state.empty()) {
        GtkWidget *state_label = label(state);
        gtk_widget_add_css_class(
            state_label,
            kernel.active
                ? "state-available"
                : "state-neutral");
        gtk_box_append(GTK_BOX(top), state_label);
    }

    GtkWidget *support =
        label(support_copy(kernel));
    gtk_widget_add_css_class(
        support,
        kernel.end_of_life
            ? "state-warning"
            : "card-copy");
    gtk_box_append(GTK_BOX(top), support);
    gtk_box_append(GTK_BOX(outer), top);

    GtkWidget *links = kernel_links(kernel);
    if (gtk_widget_get_first_child(links) != nullptr) {
        gtk_box_append(GTK_BOX(outer), links);
    }

    GtkWidget *actions =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    if (kernel.installed) {
        GtkWidget *queue =
            gtk_button_new_with_label("Queue removal");
        auto *queue_kernel = new KernelRecord(kernel);
        g_object_set_data_full(
            G_OBJECT(queue),
            "kernel-record",
            queue_kernel,
            [](gpointer pointer) {
                delete static_cast<KernelRecord *>(pointer);
            });
        gtk_widget_set_sensitive(
            queue,
            !kernel.active &&
                !kernel.remove_package_ids.empty());
        if (kernel.active) {
            gtk_widget_set_tooltip_text(
                queue,
                "The running kernel cannot be removed.");
        }
        g_signal_connect(
            queue,
            "clicked",
            G_CALLBACK(queue_remove_clicked),
            context);
        gtk_box_append(GTK_BOX(actions), queue);

        GtkWidget *remove_now =
            gtk_button_new_with_label("Remove");
        auto *remove_kernel = new KernelRecord(kernel);
        g_object_set_data_full(
            G_OBJECT(remove_now),
            "kernel-record",
            remove_kernel,
            [](gpointer pointer) {
                delete static_cast<KernelRecord *>(pointer);
            });
        gtk_widget_set_sensitive(
            remove_now,
            !kernel.active &&
                !kernel.remove_package_ids.empty());
        g_signal_connect(
            remove_now,
            "clicked",
            G_CALLBACK(remove_now_clicked),
            context);
        gtk_box_append(GTK_BOX(actions), remove_now);
    } else if (kernel.installable) {
        GtkWidget *queue =
            gtk_button_new_with_label("Queue installation");
        auto *queue_kernel = new KernelRecord(kernel);
        g_object_set_data_full(
            G_OBJECT(queue),
            "kernel-record",
            queue_kernel,
            [](gpointer pointer) {
                delete static_cast<KernelRecord *>(pointer);
            });
        g_signal_connect(
            queue,
            "clicked",
            G_CALLBACK(queue_install_clicked),
            context);
        gtk_box_append(GTK_BOX(actions), queue);

        GtkWidget *install_now =
            gtk_button_new_with_label("Install");
        auto *install_kernel = new KernelRecord(kernel);
        g_object_set_data_full(
            G_OBJECT(install_now),
            "kernel-record",
            install_kernel,
            [](gpointer pointer) {
                delete static_cast<KernelRecord *>(pointer);
            });
        g_signal_connect(
            install_now,
            "clicked",
            G_CALLBACK(install_now_clicked),
            context);
        gtk_box_append(GTK_BOX(actions), install_now);
    }

    gtk_box_append(GTK_BOX(outer), actions);

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(
        GTK_LIST_BOX_ROW(row), outer);
    return row;
}

void rebuild_kernel_rows(KernelManagerContext *context)
{
    if (context == nullptr ||
        context->kernel_list == nullptr) {
        return;
    }
    clear_list(context->kernel_list);

    std::size_t count = 0U;
    for (const KernelRecord &kernel :
         context->kernels) {
        if (!context->selected_series.empty() &&
            kernel.series != context->selected_series) {
            continue;
        }
        gtk_list_box_append(
            context->kernel_list,
            make_kernel_row(context, kernel));
        ++count;
    }

    if (count == 0U) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row),
            label("No kernels are available in this series."));
        gtk_list_box_append(context->kernel_list, row);
    }
}

void series_selected(
    GtkListBox *,
    GtkListBoxRow *row,
    gpointer user_data)
{
    auto *context =
        static_cast<KernelManagerContext *>(user_data);
    if (context == nullptr || row == nullptr) return;
    const auto *series =
        static_cast<const std::string *>(
            g_object_get_data(
                G_OBJECT(row),
                "kernel-series"));
    if (series == nullptr) return;
    context->selected_series = *series;
    rebuild_kernel_rows(context);
}

void rebuild_series(KernelManagerContext *context)
{
    if (context == nullptr ||
        context->series_list == nullptr) {
        return;
    }
    clear_list(context->series_list);

    std::vector<std::string> series;
    for (const KernelRecord &kernel :
         context->kernels) {
        if (std::find(
                series.begin(),
                series.end(),
                kernel.series) == series.end()) {
            series.push_back(kernel.series);
        }
    }

    if (!context->selected_series.empty() &&
        std::find(
            series.begin(),
            series.end(),
            context->selected_series) ==
            series.end()) {
        context->selected_series.clear();
    }

    if (context->selected_series.empty()) {
        const auto active =
            std::find_if(
                context->kernels.begin(),
                context->kernels.end(),
                [](const KernelRecord &kernel) {
                    return kernel.active;
                });
        if (active != context->kernels.end()) {
            context->selected_series =
                active->series;
        } else if (!series.empty()) {
            context->selected_series =
                series.front();
        }
    }

    GtkListBoxRow *selected_row = nullptr;
    for (const std::string &value : series) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row),
            label(value));
        auto *copy = new std::string(value);
        g_object_set_data_full(
            G_OBJECT(row),
            "kernel-series",
            copy,
            [](gpointer pointer) {
                delete static_cast<std::string *>(pointer);
            });
        gtk_list_box_append(context->series_list, row);
        if (value == context->selected_series) {
            selected_row = GTK_LIST_BOX_ROW(row);
        }
    }

    if (selected_row != nullptr) {
        gtk_list_box_select_row(
            context->series_list,
            selected_row);
    } else {
        rebuild_kernel_rows(context);
    }
}

void load_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<LoadTaskData *>(task_data);
    auto *result = new LoadResult{};
    result->generation =
        data == nullptr ? 0U : data->generation;

    if (data == nullptr) {
        result->error = "Kernel manager task state is unavailable.";
    } else {
        EngineClient engine;
        if (data->refresh_installed) {
            (void)engine.refresh_installed(result->error);
        }
        if (result->error.empty()) {
            (void)engine.list_kernels(
                data->kernel_type,
                result->kernels,
                result->error);
        }
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<LoadResult *>(pointer);
        });
}

void load_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *context =
        static_cast<KernelManagerContext *>(
            g_object_get_data(
                G_OBJECT(window),
                "kernel-manager-context"));
    auto *result =
        static_cast<LoadResult *>(
            g_task_propagate_pointer(
                G_TASK(async_result), nullptr));

    if (context == nullptr || result == nullptr) {
        delete result;
        return;
    }
    if (result->generation != context->generation) {
        delete result;
        return;
    }

    context->busy = false;
    if (!result->error.empty()) {
        set_status(
            context,
            "Unable to read kernel inventory: " +
                result->error);
        delete result;
        rebuild_queue(context);
        return;
    }

    context->kernels = std::move(result->kernels);
    delete result;

    const auto active =
        std::find_if(
            context->kernels.begin(),
            context->kernels.end(),
            [](const KernelRecord &kernel) {
                return kernel.active;
            });
    if (context->current != nullptr) {
        if (active == context->kernels.end()) {
            gtk_label_set_text(
                GTK_LABEL(context->current),
                "The running kernel was not found in repository/package state.");
        } else {
            const std::string text =
                "Currently running: " +
                active->version +
                active->kernel_type +
                " — " +
                support_copy(*active);
            gtk_label_set_text(
                GTK_LABEL(context->current),
                text.c_str());
        }
    }

    set_status(
        context,
        std::to_string(context->kernels.size()) +
        " kernel releases inspected by the native package engine.");
    rebuild_series(context);
    rebuild_queue(context);
}

void refresh_kernels(
    KernelManagerContext *context,
    const bool refresh_installed)
{
    if (context == nullptr ||
        context->window == nullptr ||
        context->busy) {
        return;
    }
    context->busy = true;
    ++context->generation;
    set_status(
        context,
        refresh_installed
            ? "Verifying installed kernel state…"
            : "Reading Linux kernel inventory…");
    rebuild_queue(context);

    auto *data = new LoadTaskData{
        context->generation,
        selected_kernel_type(context),
        refresh_installed};
    GTask *task =
        g_task_new(
            G_OBJECT(context->window),
            nullptr,
            load_complete,
            nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<LoadTaskData *>(pointer);
        });
    g_task_run_in_thread(task, load_worker);
    g_object_unref(task);
}

void type_changed(
    GtkDropDown *,
    GParamSpec *,
    gpointer user_data)
{
    auto *context =
        static_cast<KernelManagerContext *>(user_data);
    if (context == nullptr) return;
    context->selected_series.clear();
    refresh_kernels(context, false);
}

void bulk_remove_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *context =
        static_cast<KernelManagerContext *>(user_data);
    if (context == nullptr) return;

    std::size_t added = 0U;
    for (const KernelRecord &kernel :
         context->kernels) {
        if (!kernel.safe_to_remove) continue;
        queue_action(context, kernel, false);
        ++added;
    }
    if (added == 0U) {
        set_status(
            context,
            "No obsolete kernel can be removed safely. The running kernel and one older fallback are always retained.");
    } else {
        set_status(
            context,
            std::to_string(added) +
            " obsolete kernel release" +
            (added == 1U ? " was" : "s were") +
            " added to the reviewed queue.");
    }
}

void plan_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<PlanTaskData *>(task_data);
    auto *result = new PlanResult{};
    if (data == nullptr) {
        result->error =
            "Kernel transaction task state is unavailable.";
    } else {
        EngineClient engine;
        TransactionRequest request;

        if (!data->install_ids.empty()) {
            /*
             * Resolve additions and removals in one projected final state.
             * This prevents two independently valid plans from being merged
             * into a combination that the dependency solver never reviewed.
             */
            request.action = TransactionAction::install;
            request.package_ids = data->install_ids;
            request.remove_package_ids = data->remove_ids;
        } else if (!data->remove_ids.empty()) {
            request.action = TransactionAction::remove;
            request.package_ids = data->remove_ids;
        } else {
            result->error =
                "The queued kernel transaction contains no package changes.";
        }

        if (result->error.empty()) {
            std::string error;
            const auto plan =
                engine.plan(request, error);
            if (!plan.has_value()) {
                result->error =
                    "Unable to resolve the queued kernel transaction: " +
                    error;
            } else {
                result->plan = *plan;
            }
        }
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<PlanResult *>(pointer);
        });
}

/*
 * GtkDialog is retained for the supported GTK 4.6 baseline. Scope the
 * deprecation suppression to this compatibility dialog only.
 */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
GtkWidget *transaction_dialog(
    GtkWindow *parent,
    const TransactionPlan &plan)
{
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(
            "Review kernel transaction",
            parent,
            static_cast<GtkDialogFlags>(
                GTK_DIALOG_MODAL |
                GTK_DIALOG_DESTROY_WITH_PARENT),
            "Cancel", GTK_RESPONSE_CANCEL,
            "Apply", GTK_RESPONSE_ACCEPT,
            nullptr);
    gtk_window_set_default_size(
        GTK_WINDOW(dialog), 700, 500);

    GtkWidget *content =
        gtk_dialog_get_content_area(
            GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(content), 10);
    gtk_widget_set_margin_start(content, 16);
    gtk_widget_set_margin_end(content, 16);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);

    GtkWidget *intro =
        label(
            "Every package change below was resolved by the native engine. The privileged helper will re-simulate this exact plan after refreshing repository metadata and will abort on any drift.");
    gtk_label_set_wrap(GTK_LABEL(intro), true);
    gtk_box_append(GTK_BOX(content), intro);

    GtkWidget *list = gtk_list_box_new();
    for (const TransactionItem &item : plan.items) {
        std::string text =
            std::string(
                item.action == TransactionAction::remove
                    ? "Remove "
                    : item.action == TransactionAction::upgrade
                        ? "Upgrade "
                        : "Install ") +
            item.package_id;
        if (item.action == TransactionAction::remove) {
            text += " " + item.from_version;
        } else {
            text += " → " + item.to_version;
        }
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *copy = label(text);
        gtk_widget_set_margin_start(copy, 8);
        gtk_widget_set_margin_end(copy, 8);
        gtk_widget_set_margin_top(copy, 6);
        gtk_widget_set_margin_bottom(copy, 6);
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row), copy);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(content), scroll);
    return dialog;
}
G_GNUC_END_IGNORE_DEPRECATIONS

void destroy_apply(ApplyOperation *operation)
{
    if (operation == nullptr) return;
    if (operation->window != nullptr) {
        g_object_unref(operation->window);
    }
    delete operation;
}

void apply_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *operation =
        static_cast<ApplyOperation *>(user_data);
    auto *process = G_SUBPROCESS(source_object);

    GError *error = nullptr;
    gchar *stdout_text = nullptr;
    gchar *stderr_text = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process,
            async_result,
            &stdout_text,
            &stderr_text,
            &error);
    const bool success =
        communicated != FALSE &&
        g_subprocess_get_successful(process);

    if (operation != nullptr) {
        std::string message;
        if (success) {
            message =
                "Kernel transaction completed successfully.";
        } else if (
            stderr_text != nullptr &&
            *stderr_text != '\0') {
            message = stderr_text;
        } else if (
            error != nullptr &&
            error->message != nullptr) {
            message = error->message;
        } else {
            message = "Kernel transaction failed.";
        }
        record_history(
            operation->plan,
            success,
            message);

        KernelManagerContext *context =
            operation->context;
        if (context != nullptr) {
            context->busy = false;
            gtk_widget_set_sensitive(
                GTK_WIDGET(context->window), true);
            if (success) {
                context->queued.clear();
                if (context->changed_callback != nullptr) {
                    context->changed_callback(
                        context->changed_user_data);
                }
                set_status(
                    context,
                    "Kernel transaction complete. Verifying installed and available kernel state…");
                rebuild_queue(context);
                refresh_kernels(context, true);
            } else {
                set_status(
                    context,
                    "Kernel transaction failed: " +
                    message);
                rebuild_queue(context);
            }
        }
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&error);
    destroy_apply(operation);
}

void start_apply(
    KernelManagerContext *context,
    const TransactionPlan &plan)
{
    if (context == nullptr ||
        context->window == nullptr) {
        return;
    }

    std::vector<std::string> specs;
    std::string spec_error;
    if (!exact_transaction_specs(
            plan, specs, spec_error)) {
        set_status(
            context,
            spec_error.empty()
                ? "Kernel transaction contains no exact package changes."
                : spec_error.c_str());
        return;
    }

    std::vector<std::string> arguments{
        "pkexec",
        "/usr/libexec/infiltrator-software-update-helper",
        "apply-plan"};
    arguments.insert(
        arguments.end(),
        specs.begin(),
        specs.end());

    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    GError *error = nullptr;
    GSubprocess *process =
        g_subprocess_newv(
            argv.data(),
            static_cast<GSubprocessFlags>(
                G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                G_SUBPROCESS_FLAGS_STDERR_PIPE),
            &error);
    if (process == nullptr) {
        set_status(
            context,
            error != nullptr &&
                    error->message != nullptr
                ? error->message
                : "Unable to start kernel transaction.");
        g_clear_error(&error);
        return;
    }

    context->busy = true;
    gtk_widget_set_sensitive(
        GTK_WIDGET(context->window), false);
    set_status(
        context,
        "Waiting for administrator authorization and applying the exact reviewed kernel transaction…");

    auto *operation = new ApplyOperation{};
    operation->window =
        GTK_WINDOW(g_object_ref(context->window));
    operation->context = context;
    operation->plan = plan;

    g_subprocess_communicate_utf8_async(
        process,
        nullptr,
        nullptr,
        apply_complete,
        operation);
    g_object_unref(process);
}

void plan_confirm_response(
    GtkDialog *dialog,
    gint response,
    gpointer user_data)
{
    auto *result =
        static_cast<PlanResult *>(user_data);
    auto *context =
        static_cast<KernelManagerContext *>(
            g_object_get_data(
                G_OBJECT(dialog),
                "kernel-manager-context"));
    gtk_window_destroy(GTK_WINDOW(dialog));

    if (result != nullptr &&
        context != nullptr &&
        response == GTK_RESPONSE_ACCEPT &&
        result->plan.has_value()) {
        start_apply(context, *result->plan);
    }
    delete result;
}

void plan_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *context =
        static_cast<KernelManagerContext *>(
            g_object_get_data(
                G_OBJECT(window),
                "kernel-manager-context"));
    auto *result =
        static_cast<PlanResult *>(
            g_task_propagate_pointer(
                G_TASK(async_result), nullptr));

    if (context == nullptr || result == nullptr) {
        delete result;
        return;
    }

    context->busy = false;
    rebuild_queue(context);
    if (!result->plan.has_value()) {
        set_status(
            context,
            result->error.empty()
                ? "Unable to resolve kernel transaction."
                : result->error);
        delete result;
        return;
    }

    set_status(
        context,
        "Kernel transaction resolved. Review every package change before authorizing.");
    GtkWidget *dialog =
        transaction_dialog(
            context->window,
            *result->plan);
    g_object_set_data(
        G_OBJECT(dialog),
        "kernel-manager-context",
        context);
    g_signal_connect(
        dialog,
        "response",
        G_CALLBACK(plan_confirm_response),
        result);
    gtk_window_present(GTK_WINDOW(dialog));
}

void begin_kernel_actions(
    KernelManagerContext *context,
    const std::vector<QueuedKernel> &actions)
{
    if (context == nullptr ||
        actions.empty() ||
        context->busy) {
        return;
    }

    std::set<std::string> install;
    std::set<std::string> remove;
    for (const QueuedKernel &queued : actions) {
        const std::vector<std::string> &ids =
            queued.install
                ? queued.kernel.install_package_ids
                : queued.kernel.remove_package_ids;
        for (const std::string &id : ids) {
            if (queued.install) {
                install.insert(id);
            } else {
                remove.insert(id);
            }
        }
    }

    for (const std::string &id : install) {
        if (remove.find(id) != remove.end()) {
            set_status(
                context,
                "The requested kernel actions both install and remove " +
                id + ".");
            return;
        }
    }

    auto *data = new PlanTaskData{};
    data->install_ids.assign(
        install.begin(), install.end());
    data->remove_ids.assign(
        remove.begin(), remove.end());
    if (data->install_ids.empty() &&
        data->remove_ids.empty()) {
        delete data;
        set_status(
            context,
            "The requested kernel action contains no package changes.");
        return;
    }

    context->busy = true;
    rebuild_queue(context);
    set_status(
        context,
        "Resolving the complete kernel transaction and checking removal safety…");

    GTask *task =
        g_task_new(
            G_OBJECT(context->window),
            nullptr,
            plan_complete,
            nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<PlanTaskData *>(pointer);
        });
    g_task_run_in_thread(task, plan_worker);
    g_object_unref(task);
}

void begin_queued_plan(
    KernelManagerContext *context)
{
    if (context == nullptr) return;
    begin_kernel_actions(
        context,
        context->queued);
}

void apply_queue_clicked(
    GtkButton *,
    gpointer user_data)
{
    begin_queued_plan(
        static_cast<KernelManagerContext *>(
            user_data));
}

void refresh_clicked(
    GtkButton *,
    gpointer user_data)
{
    refresh_kernels(
        static_cast<KernelManagerContext *>(
            user_data),
        false);
}

void destroy_context(gpointer pointer)
{
    delete static_cast<KernelManagerContext *>(pointer);
}

GtkWidget *build_manager(
    KernelManagerContext *context)
{
    GtkWidget *root =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 14);
    gtk_widget_set_margin_end(root, 14);
    gtk_widget_set_margin_top(root, 14);
    gtk_widget_set_margin_bottom(root, 14);

    GtkWidget *header =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *title =
        label("Linux Kernels");
    gtk_widget_add_css_class(title, "title-2");
    gtk_widget_set_hexpand(title, true);
    gtk_box_append(GTK_BOX(header), title);

    GtkWidget *type_label =
        label("Kernel type");
    gtk_box_append(GTK_BOX(header), type_label);

    context->type_dropdown =
        gtk_drop_down_new_from_strings(
            kKernelTypes);
    gtk_drop_down_set_selected(
        GTK_DROP_DOWN(context->type_dropdown),
        0U);
    g_signal_connect(
        context->type_dropdown,
        "notify::selected",
        G_CALLBACK(type_changed),
        context);
    gtk_box_append(
        GTK_BOX(header),
        context->type_dropdown);

    GtkWidget *refresh =
        gtk_button_new_with_label("Refresh");
    g_signal_connect(
        refresh,
        "clicked",
        G_CALLBACK(refresh_clicked),
        context);
    gtk_box_append(GTK_BOX(header), refresh);
    gtk_box_append(GTK_BOX(root), header);

    GtkWidget *warning =
        label(
            "Kernel changes can affect boot, networking, graphics and proprietary drivers. Software never permits removal of the running kernel, and bulk cleanup retains one older installed fallback kernel.");
    gtk_label_set_wrap(GTK_LABEL(warning), true);
    gtk_widget_add_css_class(warning, "card-copy");
    gtk_box_append(GTK_BOX(root), warning);

    context->current =
        label("Reading the running kernel…");
    gtk_widget_add_css_class(
        context->current, "card-title");
    gtk_box_append(
        GTK_BOX(root), context->current);

    GtkWidget *content =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_vexpand(content, true);

    context->series_list =
        GTK_LIST_BOX(gtk_list_box_new());
    gtk_widget_set_size_request(
        GTK_WIDGET(context->series_list),
        120, -1);
    gtk_list_box_set_selection_mode(
        context->series_list,
        GTK_SELECTION_SINGLE);
    g_signal_connect(
        context->series_list,
        "row-selected",
        G_CALLBACK(series_selected),
        context);

    GtkWidget *series_scroll =
        gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(series_scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(series_scroll),
        GTK_WIDGET(context->series_list));
    gtk_box_append(GTK_BOX(content), series_scroll);

    context->kernel_list =
        GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(
        context->kernel_list,
        GTK_SELECTION_NONE);
    GtkWidget *kernel_scroll =
        gtk_scrolled_window_new();
    gtk_widget_set_hexpand(kernel_scroll, true);
    gtk_widget_set_vexpand(kernel_scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(kernel_scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(kernel_scroll),
        GTK_WIDGET(context->kernel_list));
    gtk_box_append(GTK_BOX(content), kernel_scroll);
    gtk_box_append(GTK_BOX(root), content);

    GtkWidget *queue_card =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(queue_card, "card");
    gtk_box_append(
        GTK_BOX(queue_card),
        label("Queued kernel actions"));

    context->queue_list =
        GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(
        context->queue_list,
        GTK_SELECTION_NONE);
    GtkWidget *queue_scroll =
        gtk_scrolled_window_new();
    gtk_widget_set_size_request(
        queue_scroll, -1, 125);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(queue_scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(queue_scroll),
        GTK_WIDGET(context->queue_list));
    gtk_box_append(GTK_BOX(queue_card), queue_scroll);

    GtkWidget *queue_actions =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    context->status =
        label("Kernel inventory has not been loaded.");
    gtk_label_set_wrap(
        GTK_LABEL(context->status), true);
    gtk_widget_set_hexpand(context->status, true);
    gtk_box_append(
        GTK_BOX(queue_actions),
        context->status);

    context->bulk_remove =
        gtk_button_new_with_label(
            "Queue safe obsolete removals");
    gtk_widget_set_tooltip_text(
        context->bulk_remove,
        "Queues only older superseded kernels while preserving the running kernel and one older fallback.");
    g_signal_connect(
        context->bulk_remove,
        "clicked",
        G_CALLBACK(bulk_remove_clicked),
        context);
    gtk_box_append(
        GTK_BOX(queue_actions),
        context->bulk_remove);

    context->apply_queue =
        gtk_button_new_with_label(
            "Perform queued actions");
    gtk_widget_add_css_class(
        context->apply_queue,
        "suggested-action");
    gtk_widget_set_sensitive(
        context->apply_queue, false);
    g_signal_connect(
        context->apply_queue,
        "clicked",
        G_CALLBACK(apply_queue_clicked),
        context);
    gtk_box_append(
        GTK_BOX(queue_actions),
        context->apply_queue);

    GtkWidget *help =
        gtk_link_button_new_with_label(
            "https://linuxmint-user-guide.readthedocs.io/en/latest/mintupdate.html",
            "Kernel help");
    gtk_box_append(GTK_BOX(queue_actions), help);
    gtk_box_append(
        GTK_BOX(queue_card),
        queue_actions);
    gtk_box_append(GTK_BOX(root), queue_card);

    return root;
}

} // namespace

void present_kernel_manager(
    GtkWindow *parent,
    const KernelManagerChangedCallback changed_callback,
    gpointer changed_user_data)
{
    if (parent == nullptr) return;

    GtkWidget *window =
        gtk_window_new();
    gtk_window_set_title(
        GTK_WINDOW(window),
        "Linux Kernels — Software");
    gtk_window_set_default_size(
        GTK_WINDOW(window),
        980,
        720);
    gtk_window_set_transient_for(
        GTK_WINDOW(window),
        parent);
    gtk_window_set_destroy_with_parent(
        GTK_WINDOW(window),
        true);

    auto *context =
        new KernelManagerContext{};
    context->window = GTK_WINDOW(window);
    context->parent = parent;
    context->changed_callback =
        changed_callback;
    context->changed_user_data =
        changed_user_data;
    g_object_set_data_full(
        G_OBJECT(window),
        "kernel-manager-context",
        context,
        destroy_context);

    gtk_window_set_child(
        GTK_WINDOW(window),
        build_manager(context));
    gtk_window_present(
        GTK_WINDOW(window));
    refresh_kernels(context, false);
}

} // namespace infiltrator::software
