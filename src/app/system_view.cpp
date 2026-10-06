// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/system_view.hpp"

#include "app/app_coordinator.hpp"
#include "app/kernel_manager.hpp"
#include "app/system_data.hpp"
#include "app/text_utils.hpp"
#include "app/transaction_review.hpp"
#include "app/ui_components.hpp"
#include "app/window_state.hpp"
#include "core/model.hpp"

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace infiltrator::software::app {

using infiltrator::software::PackageRecord;
using infiltrator::software::TransactionAction;
using infiltrator::software::TransactionItem;
using infiltrator::software::TransactionPlan;
using infiltrator::software::make_icon;
using infiltrator::software::make_label;
using infiltrator::software::make_page_intro;
using infiltrator::software::make_stat_card;

struct SystemResult {
    unsigned int generation{0U};
    std::vector<PackageRecord> components;
    std::vector<PackageRecord> updates;
    std::string error;
    std::string update_warning;
    bool from_engine{false};
};

struct SystemTaskData {
    unsigned int generation{0U};
    bool refresh_metadata{false};
};

std::string system_identity(const PackageRecord &package)
{
    return package.package_name.empty()
        ? package.id
        : package.package_name;
}

const char *system_icon_name(const PackageRecord &package)
{
    switch (package.kind) {
    case infiltrator::software::PackageKind::kernel:
        return "computer-symbolic";
    case infiltrator::software::PackageKind::driver:
        return "preferences-system-symbolic";
    case infiltrator::software::PackageKind::system:
        return "applications-system-symbolic";
    default:
        return "application-x-executable-symbolic";
    }
}

GtkWidget *make_system_row(
    const PackageRecord &package,
    const PackageRecord *update)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "package-row");
    gtk_widget_set_margin_top(row, 6);
    gtk_widget_set_margin_bottom(row, 6);
    gtk_widget_set_margin_start(row, 8);
    gtk_widget_set_margin_end(row, 8);

    GtkWidget *icon =
        make_icon(system_icon_name(package), 24);
    gtk_widget_add_css_class(icon, "package-icon");
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *identity =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(identity, true);

    GtkWidget *name =
        make_label(package.name.c_str(), "card-title");
    gtk_label_set_ellipsize(
        GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), name);

    std::string version = package.installed_version;
    if (update != nullptr &&
        !update->available_version.empty()) {
        version += "  →  " + update->available_version;
    }
    GtkWidget *version_label =
        make_label(version.c_str(), "card-copy");
    gtk_label_set_ellipsize(
        GTK_LABEL(version_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), version_label);

    std::string meta =
        std::string(
            infiltrator::software::package_kind_name(
                package.kind));
    if (!package.architecture.empty()) {
        meta += "  •  " + package.architecture;
    }
    if (update != nullptr) {
        std::string source_name = update->repository_origin;
        if (source_name.empty()) {
            source_name = update->repository_site;
        }
        if (source_name.empty()) {
            source_name = update->source;
        }
        if (!source_name.empty()) {
            meta += "  •  Source: " + source_name;
        }
    }
    GtkWidget *meta_label =
        make_label(meta.c_str(), "discover-meta");
    gtk_label_set_ellipsize(
        GTK_LABEL(meta_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), meta_label);

    gtk_box_append(GTK_BOX(row), identity);

    const char *status_text = "Current";
    const char *status_class = "state-success";
    if (update != nullptr) {
        status_text = update->system_critical
            ? "Recommended • System-critical"
            : "Recommended update";
        status_class = update->system_critical
            ? "state-warning"
            : "state-available";
    } else if (package.system_critical) {
        status_text = "Core system";
        status_class = "state-info";
    }

    GtkWidget *status =
        make_label(status_text, status_class);
    gtk_widget_set_valign(status, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), status);

    return row;
}

void rebuild_system(WindowState *state)
{
    if (state == nullptr || state->system_list == nullptr) {
        return;
    }

    GtkWidget *child =
        gtk_widget_get_first_child(
            GTK_WIDGET(state->system_list));
    while (child != nullptr) {
        GtkWidget *next =
            gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(state->system_list, child);
        child = next;
    }

    std::unordered_map<std::string, const PackageRecord *> updates;
    updates.reserve(state->system_update_records.size());
    std::size_t critical_count = 0U;
    for (const PackageRecord &update :
         state->system_update_records) {
        updates[system_identity(update)] = &update;
        if (update.system_critical) {
            ++critical_count;
        }
    }

    for (const PackageRecord &package :
         state->system_records) {
        const auto found =
            updates.find(system_identity(package));
        const PackageRecord *update =
            found == updates.end()
                ? nullptr
                : found->second;

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row),
            make_system_row(package, update));
        gtk_list_box_append(state->system_list, row);
    }

    if (state->system_count != nullptr) {
        const std::string count =
            std::to_string(state->system_records.size());
        gtk_label_set_text(
            GTK_LABEL(state->system_count), count.c_str());
    }
    if (state->system_updates != nullptr) {
        const std::string count =
            std::to_string(
                state->system_update_records.size());
        gtk_label_set_text(
            GTK_LABEL(state->system_updates), count.c_str());
    }
    if (state->system_critical != nullptr) {
        const std::string count =
            std::to_string(critical_count);
        gtk_label_set_text(
            GTK_LABEL(state->system_critical), count.c_str());
    }
    if (state->system_review_updates != nullptr) {
        gtk_widget_set_sensitive(
            state->system_review_updates,
            !state->system_update_records.empty());
    }
}

void system_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<SystemTaskData *>(task_data);
    auto *result = new SystemResult{};
    result->generation =
        data == nullptr ? 0U : data->generation;

    if (data == nullptr) {
        result->error = "System task state is unavailable.";
    } else {
        SystemDataResult loaded =
            load_system_data(data->refresh_metadata);
        result->components = std::move(loaded.components);
        result->updates = std::move(loaded.updates);
        result->error = std::move(loaded.error);
        result->update_warning = std::move(loaded.update_warning);
        result->from_engine = loaded.from_engine;
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<SystemResult *>(pointer);
        });
}

void system_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window),
            "infiltrator-window-state"));
    auto *result = static_cast<SystemResult *>(
        g_task_propagate_pointer(
            G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }
    if (result->generation != state->system_generation) {
        delete result;
        return;
    }

    state->system_busy = false;
    state->system_records =
        std::move(result->components);
    state->system_update_records =
        std::move(result->updates);

    const std::string error = result->error;
    const std::string warning = result->update_warning;
    const bool from_engine = result->from_engine;
    delete result;

    rebuild_system(state);

    if (state->system_status != nullptr) {
        std::ostringstream message;
        if (!error.empty()) {
            message
                << "System inventory unavailable: "
                << error;
        } else {
            message
                << state->system_records.size()
                << " kernel, driver and core system components read "
                << (from_engine
                        ? "from the shared native package engine."
                        : "from direct Debian package state.");
            if (!warning.empty()) {
                message
                    << " Update status: "
                    << warning;
            } else if (state->system_update_records.empty()) {
                message
                    << " No preferred system updates are currently available.";
            } else {
                message
                    << " "
                    << state->system_update_records.size()
                    << " preferred system update"
                    << (state->system_update_records.size() == 1U
                            ? " is"
                            : "s are")
                    << " available.";
            }
        }
        gtk_label_set_text(
            GTK_LABEL(state->system_status),
            message.str().c_str());
    }

    if (state->system_refresh != nullptr) {
        gtk_widget_set_sensitive(
            state->system_refresh, true);
    }
}

void refresh_system(
    WindowState *state,
    const bool refresh_metadata)
{
    if (state == nullptr ||
        state->window == nullptr ||
        state->system_list == nullptr ||
        state->system_busy) {
        return;
    }

    state->system_loaded = true;
    state->system_busy = true;
    ++state->system_generation;

    if (state->system_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->system_status),
            refresh_metadata
                ? "Refreshing repository state and system components…"
                : "Reading system components from shared state…");
    }
    if (state->system_refresh != nullptr) {
        gtk_widget_set_sensitive(
            state->system_refresh, false);
    }

    auto *data = new SystemTaskData{
        state->system_generation,
        refresh_metadata};
    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        system_complete,
        nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<SystemTaskData *>(pointer);
        });
    g_task_run_in_thread(task, system_worker);
    g_object_unref(task);
}

void system_refresh_clicked(
    GtkButton *,
    gpointer user_data)
{
    refresh_system(
        static_cast<WindowState *>(user_data),
        true);
}

void kernel_manager_changed(gpointer user_data)
{
    notify_kernel_state_changed(
        static_cast<WindowState *>(user_data));
}

void system_manage_kernels_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr) {
        return;
    }
    infiltrator::software::present_kernel_manager(
        state->window,
        kernel_manager_changed,
        state);
}

void system_review_updates_clicked(
    GtkButton *,
    gpointer user_data)
{
    select_app_page(
        static_cast<WindowState *>(user_data),
        AppPage::updates);
}

struct ReleaseUpgradePlanResult {
    std::string target_name;
    std::string target_codename;
    TransactionPlan plan;
    std::vector<std::string> specs;
    std::string error;
};

struct ReleaseUpgradeApply {
    GtkWindow *window{};
    WindowState *state{};
    std::vector<std::string> specs;
};

void release_upgrade_plan_worker(
    GTask *task,
    gpointer,
    gpointer,
    GCancellable *)
{
    auto *result =
        new ReleaseUpgradePlanResult{};

    gchar *stdout_text = nullptr;
    gchar *stderr_text = nullptr;
    gint wait_status = 0;
    GError *gerror = nullptr;
    gchar *argv[] = {
        const_cast<gchar *>(
            "/usr/bin/infiltrator-software-release-upgrade"),
        const_cast<gchar *>("plan"),
        nullptr
    };
    const gboolean spawned =
        g_spawn_sync(
            nullptr,
            argv,
            nullptr,
            G_SPAWN_DEFAULT,
            nullptr,
            nullptr,
            &stdout_text,
            &stderr_text,
            &wait_status,
            &gerror);

    bool success = spawned != FALSE;
    if (success) {
        success =
            g_spawn_check_wait_status(
                wait_status,
                &gerror) != FALSE;
    }

    if (!success) {
        if (stderr_text != nullptr &&
            *stderr_text != '\0') {
            result->error =
                single_line(stderr_text);
        } else if (
            gerror != nullptr &&
            gerror->message != nullptr) {
            result->error =
                gerror->message;
        } else {
            result->error =
                "Unable to calculate the release-upgrade plan.";
        }
    } else if (stdout_text != nullptr) {
        std::istringstream input(stdout_text);
        std::string line;
        while (std::getline(input, line)) {
            std::vector<std::string> fields;
            std::size_t start = 0U;
            for (;;) {
                const std::size_t tab =
                    line.find('\t', start);
                fields.push_back(
                    line.substr(
                        start,
                        tab == std::string::npos
                            ? std::string::npos
                            : tab - start));
                if (tab == std::string::npos) {
                    break;
                }
                start = tab + 1U;
            }

            if (fields.size() >= 3U &&
                fields[0] == "TARGET") {
                result->target_name = fields[1];
                result->target_codename = fields[2];
            } else if (
                fields.size() >= 2U &&
                fields[0] == "SPEC") {
                result->specs.push_back(fields[1]);
            } else if (
                fields.size() >= 7U &&
                fields[0] == "ITEM") {
                TransactionItem item;
                if (fields[1] == "Remove") {
                    item.action =
                        TransactionAction::remove;
                } else if (fields[1] == "Install") {
                    item.action =
                        TransactionAction::install;
                } else if (fields[1] == "Upgrade") {
                    item.action =
                        TransactionAction::upgrade;
                } else {
                    result->error =
                        "The release-upgrade planner returned an unknown transaction action.";
                    break;
                }
                item.package_id = fields[2];
                item.from_version = fields[3];
                item.to_version = fields[4];
                try {
                    std::size_t consumed = 0U;
                    item.download_bytes =
                        static_cast<std::uint64_t>(
                            std::stoull(
                                fields[5],
                                &consumed));
                    if (consumed != fields[5].size()) {
                        result->error =
                            "The release-upgrade planner returned an invalid download size.";
                        break;
                    }
                } catch (const std::exception &) {
                    result->error =
                        "The release-upgrade planner returned an invalid download size.";
                    break;
                }
                if (fields[6] != "0" &&
                    fields[6] != "1") {
                    result->error =
                        "The release-upgrade planner returned an invalid system-critical marker.";
                    break;
                }
                item.system_critical =
                    fields[6] == "1";
                result->plan.download_bytes +=
                    item.download_bytes;
                result->plan.touches_system =
                    result->plan.touches_system ||
                    item.system_critical;
                result->plan.items.emplace_back(
                    std::move(item));
            }
        }

        if (result->error.empty() &&
            (result->target_name.empty() ||
             result->specs.empty() ||
             result->plan.items.empty())) {
            result->error =
                "The release-upgrade planner returned an incomplete review plan.";
        }
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&gerror);

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<ReleaseUpgradePlanResult *>(
                pointer);
        });
}

void destroy_release_upgrade_apply(
    ReleaseUpgradeApply *apply)
{
    if (apply == nullptr) {
        return;
    }
    if (apply->window != nullptr) {
        g_object_unref(apply->window);
    }
    delete apply;
}

void release_upgrade_apply_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *process =
        G_SUBPROCESS(source_object);
    auto *apply =
        static_cast<ReleaseUpgradeApply *>(
            user_data);
    GError *error = nullptr;
    gchar *out = nullptr;
    gchar *err = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process,
            async_result,
            &out,
            &err,
            &error);
    const bool success =
        communicated &&
        g_subprocess_get_successful(process);

    if (apply != nullptr &&
        apply->state != nullptr &&
        apply->state->system_status != nullptr) {
        std::string message;
        if (success) {
            message =
                out != nullptr && *out != '\0'
                    ? single_line(out)
                    : "Operating-system release upgrade completed.";
        } else if (
            err != nullptr &&
            *err != '\0') {
            message =
                "Release upgrade failed: " +
                single_line(err);
        } else if (
            error != nullptr &&
            error->message != nullptr) {
            message =
                "Release upgrade failed: " +
                single_line(error->message);
        } else {
            message =
                "Release upgrade failed.";
        }
        gtk_label_set_text(
            GTK_LABEL(
                apply->state->system_status),
            message.c_str());
    }

    g_free(out);
    g_free(err);
    g_clear_error(&error);
    destroy_release_upgrade_apply(apply);
}

void release_upgrade_confirm_response(
    GtkDialog *dialog,
    const gint response_id,
    gpointer user_data)
{
    auto *apply =
        static_cast<ReleaseUpgradeApply *>(
            user_data);
    gtk_window_destroy(
        GTK_WINDOW(dialog));

    if (apply == nullptr) {
        return;
    }
    if (response_id != GTK_RESPONSE_ACCEPT) {
        destroy_release_upgrade_apply(apply);
        return;
    }

    std::vector<std::string> arguments{
        "pkexec",
        "/usr/bin/infiltrator-software-release-upgrade",
        "apply"
    };
    arguments.insert(
        arguments.end(),
        apply->specs.begin(),
        apply->specs.end());

    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument :
         arguments) {
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
        if (apply->state != nullptr &&
            apply->state->system_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(
                    apply->state->system_status),
                error != nullptr &&
                error->message != nullptr
                    ? error->message
                    : "Unable to start the release upgrade.");
        }
        g_clear_error(&error);
        destroy_release_upgrade_apply(apply);
        return;
    }

    if (apply->state != nullptr &&
        apply->state->system_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(
                apply->state->system_status),
            "Release upgrade is running. Do not shut down the computer.");
    }

    g_subprocess_communicate_utf8_async(
        process,
        nullptr,
        nullptr,
        release_upgrade_apply_complete,
        apply);
    g_object_unref(process);
}

void release_upgrade_plan_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window =
        GTK_WINDOW(source_object);
    auto *state =
        static_cast<WindowState *>(
            g_object_get_data(
                G_OBJECT(window),
                "infiltrator-window-state"));
    auto *result =
        static_cast<ReleaseUpgradePlanResult *>(
            g_task_propagate_pointer(
                G_TASK(async_result),
                nullptr));

    if (state == nullptr ||
        result == nullptr) {
        delete result;
        return;
    }

    if (!result->error.empty()) {
        if (state->system_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->system_status),
                result->error.c_str());
        }
        delete result;
        return;
    }

    auto *apply =
        new ReleaseUpgradeApply{};
    apply->window =
        GTK_WINDOW(g_object_ref(window));
    apply->state = state;
    apply->specs = result->specs;

    const std::string heading =
        "Upgrade Linux Mint to " +
        result->target_name +
        " (" +
        result->target_codename +
        ")?";
    GtkWidget *dialog =
        make_transaction_review_dialog(
            state->window,
            "Review operating-system upgrade",
            heading,
            "Upgrade release",
            result->plan);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *content =
        gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    const std::string notes_uri =
        "https://www.linuxmint.com/rel_" +
        result->target_codename + ".php";
    GtkWidget *notes = gtk_link_button_new_with_label(
        notes_uri.c_str(),
        "Read the Linux Mint release notes and known issues");
    gtk_box_append(GTK_BOX(content), notes);
    GtkWidget *acknowledge = gtk_check_button_new_with_label(
        "I have read the release notes and understand that upgrading "
        "can affect this operating system.");
    gtk_box_append(GTK_BOX(content), acknowledge);
    GtkWidget *accept = gtk_dialog_get_widget_for_response(
        GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_widget_set_sensitive(accept, false);
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_signal_connect(acknowledge, "toggled",
        G_CALLBACK(+[](GtkCheckButton *button, gpointer widget) {
            gtk_widget_set_sensitive(GTK_WIDGET(widget),
                gtk_check_button_get_active(button));
        }), accept);
    g_signal_connect(
        dialog,
        "response",
        G_CALLBACK(
            release_upgrade_confirm_response),
        apply);
    gtk_window_present(
        GTK_WINDOW(dialog));
    delete result;
}

void system_release_upgrade_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *state =
        static_cast<WindowState *>(
            user_data);
    if (state == nullptr ||
        state->window == nullptr) {
        return;
    }

    if (state->system_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->system_status),
            "Checking Linux Mint release-upgrade metadata and calculating the exact target package plan…");
    }

    GTask *task =
        g_task_new(
            G_OBJECT(state->window),
            nullptr,
            release_upgrade_plan_complete,
            nullptr);
    g_task_run_in_thread(
        task,
        release_upgrade_plan_worker);
    g_object_unref(task);
}

void system_snapshots_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *state =
        static_cast<WindowState *>(
            user_data);
    GError *error = nullptr;
    if (!g_spawn_command_line_async(
            "pkexec timeshift-gtk",
            &error) &&
        state != nullptr &&
        state->system_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->system_status),
            error != nullptr &&
            error->message != nullptr
                ? error->message
                : "Timeshift is not installed.");
    }
    g_clear_error(&error);
}

GtkWidget *make_system_page(WindowState *state)
{
    GtkWidget *page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-system");

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(
            "computer-symbolic",
            "System",
            "Kernels, drivers and core operating-system components."));

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(
        GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "COMPONENTS", "0", "stat-info",
            &state->system_count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "UPDATES", "0", "stat-operation",
            &state->system_updates),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "SYSTEM-CRITICAL", "0", "stat-warning",
            &state->system_critical),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *controls =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(controls, "card");

    state->system_status =
        make_label(
            "System inventory has not been loaded yet.",
            "card-copy");
    gtk_label_set_wrap(
        GTK_LABEL(state->system_status), true);
    gtk_widget_set_hexpand(
        state->system_status, true);
    gtk_box_append(
        GTK_BOX(controls), state->system_status);

    state->system_refresh =
        gtk_button_new_with_label("Refresh");
    gtk_widget_add_css_class(
        state->system_refresh, "control-button");
    g_signal_connect(
        state->system_refresh,
        "clicked",
        G_CALLBACK(system_refresh_clicked),
        state);
    gtk_box_append(
        GTK_BOX(controls), state->system_refresh);

    state->system_review_updates =
        gtk_button_new_with_label("Review system updates");
    gtk_widget_add_css_class(
        state->system_review_updates, "accent-button");
    gtk_widget_set_sensitive(
        state->system_review_updates, false);
    g_signal_connect(
        state->system_review_updates,
        "clicked",
        G_CALLBACK(system_review_updates_clicked),
        state);
    gtk_box_append(
        GTK_BOX(controls),
        state->system_review_updates);

    GtkWidget *kernels =
        gtk_button_new_with_label(
            "Manage kernels…");
    gtk_widget_add_css_class(
        kernels, "accent-button");
    gtk_widget_set_tooltip_text(
        kernels,
        "Inspect, install, queue and safely remove Linux kernel releases.");
    g_signal_connect(
        kernels,
        "clicked",
        G_CALLBACK(system_manage_kernels_clicked),
        state);
    gtk_box_append(
        GTK_BOX(controls), kernels);

    GtkWidget *snapshots =
        gtk_button_new_with_label(
            "Snapshots…");
    gtk_widget_add_css_class(
        snapshots, "control-button");
    g_signal_connect(
        snapshots,
        "clicked",
        G_CALLBACK(system_snapshots_clicked),
        state);
    gtk_box_append(
        GTK_BOX(controls), snapshots);

    GtkWidget *release_upgrade =
        gtk_button_new_with_label(
            "OS upgrade…");
    gtk_widget_add_css_class(
        release_upgrade, "control-button");
    g_signal_connect(
        release_upgrade,
        "clicked",
        G_CALLBACK(system_release_upgrade_clicked),
        state);
    gtk_box_append(
        GTK_BOX(controls), release_upgrade);

    gtk_box_append(GTK_BOX(page), controls);

    GtkWidget *card =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "card-info");
    gtk_widget_set_vexpand(card, true);

    GtkWidget *heading =
        make_label(
            "Installed system components",
            "card-title");
    gtk_box_append(GTK_BOX(card), heading);

    GtkWidget *list = gtk_list_box_new();
    state->system_list = GTK_LIST_BOX(list);
    gtk_widget_add_css_class(list, "package-list");
    gtk_list_box_set_selection_mode(
        state->system_list,
        GTK_SELECTION_NONE);

    GtkWidget *scroll =
        gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(card), scroll);
    gtk_box_append(GTK_BOX(page), card);

    return page;
}

} // namespace infiltrator::software::app
