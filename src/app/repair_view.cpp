// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/repair_view.hpp"

#include "app/app_coordinator.hpp"
#include "app/repair_diagnostics.hpp"
#include "app/runtime_state.hpp"
#include "app/text_utils.hpp"
#include "app/ui_components.hpp"
#include "app/window_state.hpp"

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef INFILTRATOR_SOFTWARE_VERSION
#define INFILTRATOR_SOFTWARE_VERSION "0.0.0"
#endif

namespace infiltrator::software::app {

using infiltrator::software::make_icon;
using infiltrator::software::make_label;
using infiltrator::software::make_page_intro;
using infiltrator::software::make_stat_card;

struct RepairResult {
    unsigned int generation{0U};
    std::size_t source_count{0U};
    std::size_t update_count{0U};
    bool engine_ready{false};
    bool interrupted{false};
    bool refreshed_metadata{false};
    std::vector<std::string> issues;
};

struct RepairTaskData {
    unsigned int generation{0U};
    bool refresh_metadata{false};
};

void repair_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<RepairTaskData *>(task_data);
    auto *result = new RepairResult{};
    result->generation =
        data == nullptr ? 0U : data->generation;

    if (data == nullptr) {
        result->issues.emplace_back(
            "Repair task state is unavailable.");
    } else {
        RepairInspection inspection =
            inspect_repair_state(data->refresh_metadata);
        result->source_count = inspection.source_count;
        result->update_count = inspection.update_count;
        result->engine_ready = inspection.engine_ready;
        result->interrupted = inspection.interrupted;
        result->refreshed_metadata = inspection.refreshed_metadata;
        result->issues = std::move(inspection.issues);
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<RepairResult *>(pointer);
        });
}

GtkWidget *make_repair_issue_card(
    const std::string &message,
    const bool healthy)
{
    GtkWidget *card =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "repair-health-card");
    gtk_widget_add_css_class(
        card,
        healthy ? "card-info" : "card-warning");

    GtkWidget *icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        icon_well, "repair-health-icon-well");

    GtkWidget *icon =
        make_icon(
            healthy
                ? "emblem-ok-symbolic"
                : "dialog-warning-symbolic",
            22);
    gtk_widget_add_css_class(
        icon,
        healthy ? "source-icon" : "package-icon");
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_well), icon);
    gtk_box_append(GTK_BOX(card), icon_well);

    GtkWidget *copy =
        make_label(
            message.c_str(),
            healthy ? "card-copy" : "state-warning");
    gtk_label_set_wrap(GTK_LABEL(copy), true);
    gtk_widget_set_hexpand(copy, true);
    gtk_box_append(GTK_BOX(card), copy);
    return card;
}

void rebuild_repair(
    WindowState *state,
    const RepairResult &result)
{
    if (state == nullptr ||
        state->repair_list == nullptr) {
        return;
    }

    GtkWidget *child =
        gtk_widget_get_first_child(
            GTK_WIDGET(state->repair_list));
    while (child != nullptr) {
        GtkWidget *next =
            gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(state->repair_list, child);
        child = next;
    }

    if (result.issues.empty()) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row),
            make_repair_issue_card(
                "No active package, repository or interrupted-transaction problems were detected.",
                true));
        gtk_list_box_append(state->repair_list, row);
        return;
    }

    for (const std::string &issue : result.issues) {
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row),
            make_repair_issue_card(issue, false));
        gtk_list_box_append(state->repair_list, row);
    }
}

void repair_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state =
        static_cast<WindowState *>(
            g_object_get_data(
                G_OBJECT(window),
                "infiltrator-window-state"));
    auto *result =
        static_cast<RepairResult *>(
            g_task_propagate_pointer(
                G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }
    if (result->generation != state->repair_generation) {
        delete result;
        return;
    }

    state->repair_busy = false;
    state->repair_interrupted = result->interrupted;
    rebuild_repair(state, *result);

    if (state->discover_health_summary != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover_health_summary),
            result->issues.empty()
                ? "Good"
                : "Attention");
    }

    if (state->discover_health_banner_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(
                state->discover_health_banner_state),
            result->issues.empty()
                ? "No package-state issues detected."
                : "Repair attention is required.");
    }

    if (state->repair_health_banner != nullptr) {
        gtk_widget_remove_css_class(
            state->repair_health_banner,
            "repair-overview-checking");
        gtk_widget_remove_css_class(
            state->repair_health_banner,
            "repair-overview-good");
        gtk_widget_remove_css_class(
            state->repair_health_banner,
            "repair-overview-attention");
        gtk_widget_add_css_class(
            state->repair_health_banner,
            result->issues.empty()
                ? "repair-overview-good"
                : "repair-overview-attention");
    }
    if (state->repair_health_icon != nullptr) {
        gtk_image_set_from_icon_name(
            GTK_IMAGE(state->repair_health_icon),
            result->issues.empty()
                ? "emblem-ok-symbolic"
                : "dialog-warning-symbolic");
    }
    if (state->repair_health_title != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->repair_health_title),
            result->issues.empty()
                ? "Software health is good"
                : "Repair attention required");
    }
    if (state->repair_health_copy != nullptr) {
        std::string health_copy;
        if (result->issues.empty()) {
            health_copy =
                "Package state is coherent, repositories are readable and no interrupted transaction was detected.";
        } else {
            health_copy =
                std::to_string(result->issues.size()) +
                (result->issues.size() == 1U
                    ? " diagnostic needs attention before Software can report a clean state."
                    : " diagnostics need attention before Software can report a clean state.");
        }
        gtk_label_set_text(
            GTK_LABEL(state->repair_health_copy),
            health_copy.c_str());
    }

    if (state->repair_engine != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->repair_engine),
            result->engine_ready ? "Ready" : "Attention");
    }
    if (state->repair_sources != nullptr) {
        const std::string count =
            std::to_string(result->source_count);
        gtk_label_set_text(
            GTK_LABEL(state->repair_sources),
            count.c_str());
    }
    if (state->repair_issues != nullptr) {
        const std::string count =
            std::to_string(result->issues.size());
        gtk_label_set_text(
            GTK_LABEL(state->repair_issues),
            count.c_str());
    }

    if (result->issues.empty()) {
        set_software_update_runtime_state({});
    }

    if (state->repair_status != nullptr) {
        std::string message;
        if (result->issues.empty()) {
            message =
                result->refreshed_metadata
                    ? "Repository metadata and installed package state were rebuilt successfully."
                    : "Package state is coherent. " +
                        std::to_string(result->update_count) +
                        (result->update_count == 1U
                             ? " preferred update remains."
                             : " preferred updates remain.");
        } else {
            message =
                std::to_string(result->issues.size()) +
                (result->issues.size() == 1U
                     ? " active diagnostic needs attention."
                     : " active diagnostics need attention.");
        }
        gtk_label_set_text(
            GTK_LABEL(state->repair_status),
            message.c_str());
    }

    if (state->repair_recheck != nullptr) {
        gtk_widget_set_sensitive(
            state->repair_recheck, true);
    }
    if (state->repair_rebuild != nullptr) {
        gtk_widget_set_sensitive(
            state->repair_rebuild, true);
    }
    if (state->repair_configure != nullptr) {
        gtk_widget_set_sensitive(
            state->repair_configure,
            state->repair_interrupted);
    }

    delete result;
}

void refresh_repair(
    WindowState *state,
    const bool refresh_metadata)
{
    if (state == nullptr || state->window == nullptr ||
        state->repair_list == nullptr ||
        state->repair_busy) {
        return;
    }

    state->repair_loaded = true;
    state->repair_busy = true;
    ++state->repair_generation;

    if (state->repair_health_banner != nullptr) {
        gtk_widget_remove_css_class(
            state->repair_health_banner,
            "repair-overview-good");
        gtk_widget_remove_css_class(
            state->repair_health_banner,
            "repair-overview-attention");
        gtk_widget_add_css_class(
            state->repair_health_banner,
            "repair-overview-checking");
    }
    if (state->repair_health_icon != nullptr) {
        gtk_image_set_from_icon_name(
            GTK_IMAGE(state->repair_health_icon),
            "view-refresh-symbolic");
    }
    if (state->repair_health_title != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->repair_health_title),
            refresh_metadata
                ? "Rebuilding software health"
                : "Checking software health");
    }
    if (state->repair_health_copy != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->repair_health_copy),
            refresh_metadata
                ? "Repository metadata and installed package state are being rebuilt and verified."
                : "Package engine, repositories and interrupted transactions are being checked.");
    }

    if (state->repair_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->repair_status),
            refresh_metadata
                ? "Rebuilding verified repository and installed package state…"
                : "Checking package, repository and interrupted-transaction state…");
    }
    if (state->repair_recheck != nullptr) {
        gtk_widget_set_sensitive(
            state->repair_recheck, false);
    }
    if (state->repair_rebuild != nullptr) {
        gtk_widget_set_sensitive(
            state->repair_rebuild, false);
    }
    if (state->repair_configure != nullptr) {
        gtk_widget_set_sensitive(
            state->repair_configure, false);
    }

    auto *data = new RepairTaskData{
        state->repair_generation,
        refresh_metadata};
    GTask *task =
        g_task_new(
            G_OBJECT(state->window),
            nullptr,
            repair_complete,
            nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<RepairTaskData *>(pointer);
        });
    g_task_run_in_thread(task, repair_worker);
    g_object_unref(task);
}

void repair_recheck_clicked(
    GtkButton *,
    gpointer user_data)
{
    refresh_repair(
        static_cast<WindowState *>(user_data),
        false);
}

void repair_rebuild_clicked(
    GtkButton *,
    gpointer user_data)
{
    refresh_repair(
        static_cast<WindowState *>(user_data),
        true);
}

struct RepairProcessRun {
    GtkWindow *window{};
};

void destroy_repair_process_run(
    RepairProcessRun *run)
{
    if (run == nullptr) {
        return;
    }
    if (run->window != nullptr) {
        g_object_unref(run->window);
    }
    delete run;
}

void repair_configure_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *run =
        static_cast<RepairProcessRun *>(user_data);
    auto *process =
        G_SUBPROCESS(source_object);

    GError *error = nullptr;
    gchar *standard_output = nullptr;
    gchar *standard_error = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process,
            async_result,
            &standard_output,
            &standard_error,
            &error);
    const bool success =
        communicated != FALSE &&
        g_subprocess_get_successful(process);

    auto *state =
        run == nullptr || run->window == nullptr
            ? nullptr
            : static_cast<WindowState *>(
                  g_object_get_data(
                      G_OBJECT(run->window),
                      "infiltrator-window-state"));

    if (state != nullptr) {
        state->repair_busy = false;
        if (state->repair_status != nullptr) {
            if (success) {
                gtk_label_set_text(
                    GTK_LABEL(state->repair_status),
                    "Interrupted package configuration finished. Rechecking package state…");
            } else {
                std::string message =
                    "Unable to finish interrupted package configuration.";
                if (standard_error != nullptr &&
                    *standard_error != '\0') {
                    message += " ";
                    message += single_line(standard_error);
                } else if (
                    error != nullptr &&
                    error->message != nullptr) {
                    message += " ";
                    message += single_line(error->message);
                }
                gtk_label_set_text(
                    GTK_LABEL(state->repair_status),
                    message.c_str());
            }
        }

        if (success) {
            set_software_update_runtime_state({});
            notify_repair_state_changed(state);
        } else {
            if (state->repair_recheck != nullptr) {
                gtk_widget_set_sensitive(
                    state->repair_recheck, true);
            }
            if (state->repair_rebuild != nullptr) {
                gtk_widget_set_sensitive(
                    state->repair_rebuild, true);
            }
            if (state->repair_configure != nullptr) {
                gtk_widget_set_sensitive(
                    state->repair_configure,
                    state->repair_interrupted);
            }
        }
    }

    g_free(standard_output);
    g_free(standard_error);
    g_clear_error(&error);
    destroy_repair_process_run(run);
}

void repair_configure_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *state =
        static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr ||
        state->repair_busy ||
        !state->repair_interrupted) {
        return;
    }

    static const gchar *argv[] = {
        "pkexec",
        "/usr/libexec/infiltrator-software-update-helper",
        "repair-configure",
        nullptr
    };

    GError *error = nullptr;
    GSubprocess *process =
        g_subprocess_newv(
            argv,
            static_cast<GSubprocessFlags>(
                G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                G_SUBPROCESS_FLAGS_STDERR_PIPE),
            &error);
    if (process == nullptr) {
        if (state->repair_status != nullptr) {
            std::string message =
                "Unable to start package configuration repair.";
            if (error != nullptr &&
                error->message != nullptr) {
                message += " ";
                message += single_line(error->message);
            }
            gtk_label_set_text(
                GTK_LABEL(state->repair_status),
                message.c_str());
        }
        g_clear_error(&error);
        return;
    }

    state->repair_busy = true;
    if (state->repair_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->repair_status),
            "Waiting for administrator authorization to finish interrupted package configuration…");
    }
    if (state->repair_recheck != nullptr) {
        gtk_widget_set_sensitive(
            state->repair_recheck, false);
    }
    if (state->repair_rebuild != nullptr) {
        gtk_widget_set_sensitive(
            state->repair_rebuild, false);
    }
    if (state->repair_configure != nullptr) {
        gtk_widget_set_sensitive(
            state->repair_configure, false);
    }

    auto *run = new RepairProcessRun{
        GTK_WINDOW(g_object_ref(state->window))};
    g_subprocess_communicate_utf8_async(
        process,
        nullptr,
        nullptr,
        repair_configure_complete,
        run);
    g_object_unref(process);
}

std::string tail_text_file(
    const std::filesystem::path &path,
    const std::size_t maximum = 64U * 1024U)
{
    std::ifstream input(
        path,
        std::ios::binary);
    if (!input) {
        return {};
    }

    input.seekg(
        0,
        std::ios::end);
    const std::streamoff length =
        input.tellg();
    const std::streamoff start =
        length > static_cast<std::streamoff>(
                     maximum)
            ? length -
                  static_cast<std::streamoff>(
                      maximum)
            : 0;
    input.seekg(
        start,
        std::ios::beg);

    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

void repair_log_clicked(
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

    GtkWidget *window =
        gtk_window_new();
    gtk_window_set_title(
        GTK_WINDOW(window),
        "Software diagnostics log");
    gtk_window_set_transient_for(
        GTK_WINDOW(window),
        state->window);
    gtk_window_set_destroy_with_parent(
        GTK_WINDOW(window),
        true);
    gtk_window_set_default_size(
        GTK_WINDOW(window),
        900,
        650);

    std::ostringstream log;
    log << "Infiltrator Software "
        << INFILTRATOR_SOFTWARE_VERSION
        << "\n\n";

    const std::filesystem::path runtime =
        software_update_runtime_state_path();
    if (!runtime.empty()) {
        const std::string state_text =
            tail_text_file(
                runtime,
                4096U);
        if (!state_text.empty()) {
            log << "Current Software state\n"
                << "----------------------\n"
                << state_text
                << "\n";
        }
    }

    const std::string apt =
        tail_text_file(
            "/var/log/apt/history.log");
    if (!apt.empty()) {
        log << "APT transaction history\n"
            << "-----------------------\n"
            << apt
            << "\n";
    }

    const std::string dpkg =
        tail_text_file(
            "/var/log/dpkg.log");
    if (!dpkg.empty()) {
        log << "dpkg activity\n"
            << "-------------\n"
            << dpkg
            << "\n";
    }

    if (apt.empty() &&
        dpkg.empty()) {
        log << "No host package-manager log files are readable. "
            << "Software's structured transaction records remain available on the History page.\n";
    }

    GtkWidget *scroll =
        gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    GtkWidget *view =
        gtk_text_view_new();
    gtk_text_view_set_editable(
        GTK_TEXT_VIEW(view),
        false);
    gtk_text_view_set_monospace(
        GTK_TEXT_VIEW(view),
        true);
    gtk_text_view_set_wrap_mode(
        GTK_TEXT_VIEW(view),
        GTK_WRAP_NONE);
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(view)),
        log.str().c_str(),
        -1);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        view);
    gtk_window_set_child(
        GTK_WINDOW(window),
        scroll);
    gtk_window_present(
        GTK_WINDOW(window));
}

GtkWidget *make_repair_page(
    WindowState *state)
{
    GtkWidget *page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-repair");

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(
            "dialog-warning-symbolic",
            "Repair",
            "Diagnose and recover package, repository and interrupted transaction state."));

    state->repair_health_banner =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(
        state->repair_health_banner,
        "repair-overview");
    gtk_widget_add_css_class(
        state->repair_health_banner,
        "repair-overview-checking");

    GtkWidget *health_icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        health_icon_well,
        "repair-overview-icon-well");
    state->repair_health_icon =
        make_icon("view-refresh-symbolic", 30);
    gtk_widget_set_halign(
        state->repair_health_icon,
        GTK_ALIGN_CENTER);
    gtk_widget_set_valign(
        state->repair_health_icon,
        GTK_ALIGN_CENTER);
    gtk_box_append(
        GTK_BOX(health_icon_well),
        state->repair_health_icon);
    gtk_box_append(
        GTK_BOX(state->repair_health_banner),
        health_icon_well);

    GtkWidget *health_copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(
        health_copy, true);
    state->repair_health_title =
        make_label(
            "Checking software health",
            "repair-overview-title");
    state->repair_health_copy =
        make_label(
            "Package engine, repositories and interrupted transactions are being checked.",
            "repair-overview-copy");
    gtk_label_set_wrap(
        GTK_LABEL(state->repair_health_copy), true);
    gtk_box_append(
        GTK_BOX(health_copy),
        state->repair_health_title);
    gtk_box_append(
        GTK_BOX(health_copy),
        state->repair_health_copy);
    gtk_box_append(
        GTK_BOX(state->repair_health_banner),
        health_copy);
    gtk_box_append(
        GTK_BOX(page),
        state->repair_health_banner);

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(
        GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "PACKAGE ENGINE",
            "Checking",
            "stat-info",
            &state->repair_engine),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "SOURCES",
            "0",
            "stat-info",
            &state->repair_sources),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "ACTIVE ISSUES",
            "0",
            "stat-warning",
            &state->repair_issues),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *controls =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(controls, "card");
    gtk_widget_add_css_class(controls, "repair-action-panel");

    GtkWidget *repair_heading =
        make_label("Recovery actions", "card-title");
    GtkWidget *repair_copy =
        make_label(
            "Diagnose first, then use only the recovery action that matches the detected state.",
            "card-copy");
    gtk_label_set_wrap(GTK_LABEL(repair_copy), true);
    gtk_box_append(GTK_BOX(controls), repair_heading);
    gtk_box_append(GTK_BOX(controls), repair_copy);

    state->repair_status =
        make_label(
            "Repair diagnostics have not been run yet.",
            "card-copy");
    gtk_label_set_wrap(
        GTK_LABEL(state->repair_status), true);
    gtk_box_append(
        GTK_BOX(controls),
        state->repair_status);

    GtkWidget *buttons =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    state->repair_recheck =
        gtk_button_new_with_label("Recheck");
    gtk_widget_add_css_class(
        state->repair_recheck,
        "control-button");
    g_signal_connect(
        state->repair_recheck,
        "clicked",
        G_CALLBACK(repair_recheck_clicked),
        state);
    gtk_box_append(
        GTK_BOX(buttons),
        state->repair_recheck);

    state->repair_rebuild =
        gtk_button_new_with_label(
            "Rebuild repository state");
    gtk_widget_add_css_class(
        state->repair_rebuild,
        "control-button");
    g_signal_connect(
        state->repair_rebuild,
        "clicked",
        G_CALLBACK(repair_rebuild_clicked),
        state);
    gtk_box_append(
        GTK_BOX(buttons),
        state->repair_rebuild);

    state->repair_configure =
        gtk_button_new_with_label(
            "Finish interrupted configuration");
    gtk_widget_add_css_class(
        state->repair_configure,
        "accent-button");
    gtk_widget_set_sensitive(
        state->repair_configure, false);
    g_signal_connect(
        state->repair_configure,
        "clicked",
        G_CALLBACK(repair_configure_clicked),
        state);
    gtk_box_append(
        GTK_BOX(buttons),
        state->repair_configure);

    GtkWidget *diagnostics =
        gtk_button_new_with_label(
            "Diagnostics log…");
    gtk_widget_add_css_class(
        diagnostics,
        "control-button");
    g_signal_connect(
        diagnostics,
        "clicked",
        G_CALLBACK(repair_log_clicked),
        state);
    gtk_box_append(
        GTK_BOX(buttons),
        diagnostics);

    gtk_box_append(
        GTK_BOX(controls), buttons);
    gtk_box_append(
        GTK_BOX(page), controls);

    GtkWidget *list = gtk_list_box_new();
    state->repair_list = GTK_LIST_BOX(list);
    gtk_widget_add_css_class(list, "package-list");
    gtk_widget_add_css_class(list, "repair-list");
    gtk_list_box_set_selection_mode(
        state->repair_list,
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
    gtk_box_append(GTK_BOX(page), scroll);

    return page;
}

} // namespace infiltrator::software::app
