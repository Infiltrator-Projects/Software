// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/repository_view.hpp"

#include "app/repository_controller.hpp"
#include "app/ui_components.hpp"
#include "app/window_state.hpp"
#include "sources/source_inventory.hpp"

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <string>
#include <utility>
#include <vector>

namespace infiltrator::software::app {

using infiltrator::software::SourceKind;
using infiltrator::software::SourceRecord;
using infiltrator::software::configure_repository_controller;
using infiltrator::software::make_icon;
using infiltrator::software::make_label;
using infiltrator::software::make_page_intro;
using infiltrator::software::make_stat_card;
using infiltrator::software::refresh_repository_controller;
using infiltrator::software::source_kind_name;

void refresh_discover(WindowState *state, bool force_refresh);
void refresh_updates(WindowState *state, bool refresh_metadata);
void rebuild_discover_repository_preview(WindowState *state);
std::string one_line(std::string value);

struct AddSourceDialog {
    GtkWindow *window{};
    GtkWindow *main_window{};
    GtkWidget *type{};
    GtkWidget *name{};
    GtkWidget *url{};
    GtkWidget *suite{};
    GtkWidget *components{};
    GtkWidget *signed_by{};
    GtkWidget *status{};
};

struct AddSourceRun {
    GtkWindow *dialog{};
    GtkWindow *main_window{};
};

void destroy_add_source_dialog(gpointer data)
{
    delete static_cast<AddSourceDialog *>(data);
}

void destroy_add_source_run(AddSourceRun *run)
{
    if (run == nullptr) {
        return;
    }
    if (run->dialog != nullptr) {
        g_object_unref(run->dialog);
    }
    if (run->main_window != nullptr) {
        g_object_unref(run->main_window);
    }
    delete run;
}

std::string entry_text(GtkWidget *widget)
{
    if (widget == nullptr || !GTK_IS_EDITABLE(widget)) {
        return {};
    }
    const char *text = gtk_editable_get_text(GTK_EDITABLE(widget));
    return text == nullptr ? std::string{} : std::string{text};
}

void add_source_process_complete(
    GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
    auto *run = static_cast<AddSourceRun *>(user_data);
    auto *process = G_SUBPROCESS(source_object);

    GError *error = nullptr;
    gchar *stdout_text = nullptr;
    gchar *stderr_text = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process,
            result,
            &stdout_text,
            &stderr_text,
            &error);

    auto *state = run == nullptr || run->main_window == nullptr
        ? nullptr
        : static_cast<WindowState *>(
              g_object_get_data(
                  G_OBJECT(run->main_window),
                  "infiltrator-window-state"));

    auto *dialog_context =
        run == nullptr || run->dialog == nullptr
            ? nullptr
            : static_cast<AddSourceDialog *>(
                  g_object_get_data(
                      G_OBJECT(run->dialog),
                      "add-source-context"));

    bool success = communicated &&
                   g_subprocess_get_successful(process);

    if (dialog_context != nullptr &&
        dialog_context->status != nullptr) {
        if (success) {
            gtk_label_set_text(
                GTK_LABEL(dialog_context->status),
                "Source added. Refreshing repositories and Discover…");
        } else {
            std::string message = "Unable to add source.";
            if (error != nullptr && error->message != nullptr) {
                message += " ";
                message += error->message;
            } else if (stderr_text != nullptr &&
                       *stderr_text != '\0') {
                message += " ";
                message += stderr_text;
            }
            gtk_label_set_text(
                GTK_LABEL(dialog_context->status),
                message.c_str());
        }
    }

    if (success && state != nullptr) {
        refresh_repositories(state);
        refresh_discover(state, true);
        if (run != nullptr && run->dialog != nullptr) {
            gtk_window_destroy(run->dialog);
        }
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&error);
    destroy_add_source_run(run);
}

void add_source_submit(GtkButton *, gpointer user_data)
{
    auto *context = static_cast<AddSourceDialog *>(user_data);
    if (context == nullptr ||
        context->window == nullptr ||
        context->main_window == nullptr) {
        return;
    }

    const std::string name = entry_text(context->name);
    const std::string url = entry_text(context->url);
    const std::string suite = entry_text(context->suite);
    const std::string components = entry_text(context->components);
    const std::string signed_by = entry_text(context->signed_by);
    const guint selected =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(context->type));

    if (name.empty() || url.rfind("https://", 0U) != 0U) {
        gtk_label_set_text(
            GTK_LABEL(context->status),
            "Name is required and the source URL must use HTTPS.");
        return;
    }

    std::vector<std::string> arguments;
    if (selected == 0U) {
        if (suite.empty() || components.empty()) {
            gtk_label_set_text(
                GTK_LABEL(context->status),
                "APT sources require a suite and at least one component.");
            return;
        }
        arguments = {
            "pkexec",
            "/usr/libexec/infiltrator-software-helper",
            "add-apt-source",
            name,
            url,
            suite,
            components,
            signed_by
        };
    } else {
        arguments = {
            "flatpak",
            "remote-add",
            "--user",
            "--if-not-exists",
            name,
            url
        };
    }

    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    GError *error = nullptr;
    GSubprocess *process = g_subprocess_newv(
        argv.data(),
        static_cast<GSubprocessFlags>(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error);

    if (process == nullptr) {
        std::string message = "Unable to start source management.";
        if (error != nullptr && error->message != nullptr) {
            message += " ";
            message += error->message;
        }
        gtk_label_set_text(
            GTK_LABEL(context->status),
            message.c_str());
        g_clear_error(&error);
        return;
    }

    gtk_label_set_text(
        GTK_LABEL(context->status),
        selected == 0U
            ? "Waiting for administrator authorization…"
            : "Adding Flatpak remote…");

    auto *run = new AddSourceRun{};
    run->dialog = GTK_WINDOW(g_object_ref(context->window));
    run->main_window =
        GTK_WINDOW(g_object_ref(context->main_window));

    g_subprocess_communicate_utf8_async(
        process,
        nullptr,
        nullptr,
        add_source_process_complete,
        run);
    g_object_unref(process);
}

void add_source_type_changed(
    GObject *object,
    GParamSpec *,
    gpointer user_data)
{
    auto *context = static_cast<AddSourceDialog *>(user_data);
    if (context == nullptr) {
        return;
    }

    const bool apt =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(object)) == 0U;
    gtk_widget_set_sensitive(context->suite, apt);
    gtk_widget_set_sensitive(context->components, apt);
    gtk_widget_set_sensitive(context->signed_by, apt);

    if (context->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(context->status),
            apt
                ? "APT sources are written as modern .sources files and require administrator authorization."
                : "Flatpak remotes are added for your user account and do not require administrator authorization.");
    }
}

GtkWidget *form_row(const char *caption, GtkWidget *control)
{
    GtkWidget *row =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label =
        make_label(caption, "detail-label");
    gtk_widget_set_size_request(label, 120, -1);
    gtk_widget_set_hexpand(control, true);
    gtk_box_append(GTK_BOX(row), label);
    gtk_box_append(GTK_BOX(row), control);
    return row;
}

void add_source_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr) {
        return;
    }

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Add Software Source");
    gtk_window_set_default_size(GTK_WINDOW(window), 620, 440);
    gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), true);
    gtk_window_set_modal(GTK_WINDOW(window), true);

    auto *context = new AddSourceDialog{};
    context->window = GTK_WINDOW(window);
    context->main_window = state->window;
    g_object_set_data_full(
        G_OBJECT(window),
        "add-source-context",
        context,
        destroy_add_source_dialog);

    GtkWidget *outer =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 24);
    gtk_widget_set_margin_end(outer, 24);
    gtk_widget_set_margin_top(outer, 24);
    gtk_widget_set_margin_bottom(outer, 24);
    gtk_window_set_child(GTK_WINDOW(window), outer);

    gtk_box_append(
        GTK_BOX(outer),
        make_page_intro(
            "list-add-symbolic",
            "Add Software Source",
            "Add an APT repository or Flatpak remote to Software."));

    static const char *types[] = {
        "APT repository",
        "Flatpak remote",
        nullptr
    };
    context->type = gtk_drop_down_new_from_strings(types);
    gtk_box_append(
        GTK_BOX(outer),
        form_row("Type", context->type));

    context->name = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->name), "e.g. flathub or vendor-name");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("Name", context->name));

    context->url = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->url), "https://…");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("URL", context->url));

    context->suite = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->suite), "e.g. noble, stable");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("APT suite", context->suite));

    context->components = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->components), "e.g. main universe");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("Components", context->components));

    context->signed_by = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->signed_by),
        "/etc/apt/keyrings/vendor.gpg (optional)");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("Signed by", context->signed_by));

    context->status = make_label(
        "APT sources are written as modern .sources files and require administrator authorization.",
        "discover-status");
    gtk_label_set_wrap(GTK_LABEL(context->status), true);
    gtk_box_append(GTK_BOX(outer), context->status);

    GtkWidget *actions =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(
        cancel,
        "clicked",
        G_CALLBACK(gtk_window_destroy),
        window);
    gtk_box_append(GTK_BOX(actions), cancel);

    GtkWidget *add = gtk_button_new_with_label("Add Source");
    gtk_widget_add_css_class(add, "suggested-action");
    g_signal_connect(
        add,
        "clicked",
        G_CALLBACK(add_source_submit),
        context);
    gtk_box_append(GTK_BOX(actions), add);
    gtk_box_append(GTK_BOX(outer), actions);

    g_signal_connect(
        context->type,
        "notify::selected",
        G_CALLBACK(add_source_type_changed),
        context);

    gtk_window_present(GTK_WINDOW(window));
}

const char *source_icon(const SourceRecord &source) noexcept
{
    switch (source.kind) {
    case infiltrator::software::SourceKind::infiltrator:
        return "emblem-default-symbolic";
    case infiltrator::software::SourceKind::apt:
        return "package-x-generic-symbolic";
    case infiltrator::software::SourceKind::flatpak:
        return "package-x-generic-symbolic";
    }
    return "network-workgroup-symbolic";
}

struct SourceToggleContext {
    GtkWindow *window{};
    SourceRecord source;
};

struct SourceToggleRun {
    GtkWindow *window{};
    bool enabled{false};
    std::string source_name;
};

void destroy_source_toggle_context(gpointer data)
{
    delete static_cast<SourceToggleContext *>(data);
}

void destroy_source_toggle_run(SourceToggleRun *run)
{
    if (run == nullptr) {
        return;
    }
    if (run->window != nullptr) {
        g_object_unref(run->window);
    }
    delete run;
}

void source_toggle_process_complete(
    GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
    auto *run = static_cast<SourceToggleRun *>(user_data);
    auto *process = G_SUBPROCESS(source_object);

    GError *error = nullptr;
    gchar *stdout_text = nullptr;
    gchar *stderr_text = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process,
            result,
            &stdout_text,
            &stderr_text,
            &error);
    const bool success =
        communicated &&
        g_subprocess_get_successful(process);

    auto *state =
        run == nullptr || run->window == nullptr
            ? nullptr
            : static_cast<WindowState *>(
                  g_object_get_data(
                      G_OBJECT(run->window),
                      "infiltrator-window-state"));

    if (state != nullptr) {
        state->repositories.busy = false;
        if (state->repositories.flow != nullptr) {
            gtk_widget_set_sensitive(
                state->repositories.flow, true);
        }

        if (success) {
            if (state->repositories.status != nullptr) {
                const std::string message =
                    run->source_name +
                    (run->enabled
                         ? " enabled. Refreshing software state…"
                         : " disabled. Refreshing software state…");
                gtk_label_set_text(
                    GTK_LABEL(state->repositories.status),
                    message.c_str());
            }

            refresh_repositories(state);
            if (state->discover_loaded) {
                refresh_discover(state, true);
            }
            if (state->updates_loaded) {
                refresh_updates(state, true);
            }
        } else if (state->repositories.status != nullptr) {
            std::string message =
                run != nullptr && run->enabled
                    ? "Unable to enable source."
                    : "Unable to disable source.";
            if (stderr_text != nullptr &&
                *stderr_text != '\0') {
                message += " ";
                message += one_line(stderr_text);
            } else if (error != nullptr &&
                       error->message != nullptr) {
                message += " ";
                message += one_line(error->message);
            }
            gtk_label_set_text(
                GTK_LABEL(state->repositories.status),
                message.c_str());
        }
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&error);
    destroy_source_toggle_run(run);
}

void source_toggle_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *context =
        static_cast<SourceToggleContext *>(user_data);
    if (context == nullptr ||
        context->window == nullptr) {
        return;
    }

    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(context->window),
            "infiltrator-window-state"));
    if (state == nullptr || state->repositories.busy) {
        return;
    }

    const bool enable = !context->source.enabled;
    std::vector<std::string> arguments;

    if (context->source.kind ==
        infiltrator::software::SourceKind::apt) {
        if (context->source.backing_file.empty() ||
            context->source.entry_index == 0U) {
            if (state->repositories.status != nullptr) {
                gtk_label_set_text(
                    GTK_LABEL(state->repositories.status),
                    "This APT source has no mutable source-file identity.");
            }
            return;
        }
        arguments = {
            "pkexec",
            "/usr/libexec/infiltrator-software-helper",
            "set-apt-source-enabled",
            context->source.backing_file,
            std::to_string(context->source.entry_index),
            enable ? "yes" : "no",
            context->source.location,
            context->source.apt_suites
        };
    } else if (context->source.kind ==
               infiltrator::software::SourceKind::flatpak) {
        const bool system_scope =
            context->source.scope == "System";
        if (system_scope) {
            arguments = {
                "pkexec",
                "/usr/bin/flatpak",
                "remote-modify",
                "--system",
                enable ? "--enable" : "--disable",
                context->source.name
            };
        } else {
            arguments = {
                "flatpak",
                "remote-modify",
                "--user",
                enable ? "--enable" : "--disable",
                context->source.name
            };
        }
    } else {
        return;
    }

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
        if (state->repositories.status != nullptr) {
            std::string message =
                enable
                    ? "Unable to enable source."
                    : "Unable to disable source.";
            if (error != nullptr &&
                error->message != nullptr) {
                message += " ";
                message += one_line(error->message);
            }
            gtk_label_set_text(
                GTK_LABEL(state->repositories.status),
                message.c_str());
        }
        g_clear_error(&error);
        return;
    }

    state->repositories.busy = true;
    if (state->repositories.flow != nullptr) {
        gtk_widget_set_sensitive(
            state->repositories.flow, false);
    }
    if (state->repositories.status != nullptr) {
        const std::string message =
            std::string(enable ? "Enabling " : "Disabling ") +
            context->source.name + "…";
        gtk_label_set_text(
            GTK_LABEL(state->repositories.status),
            message.c_str());
    }

    auto *run = new SourceToggleRun{
        context->window,
        enable,
        context->source.name};
    g_object_ref(run->window);
    g_subprocess_communicate_utf8_async(
        process,
        nullptr,
        nullptr,
        source_toggle_process_complete,
        run);
    g_object_unref(process);
}

GtkWidget *make_source_card(
    WindowState *state,
    const SourceRecord &source)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_widget_add_css_class(card, "source-card");

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *icon = make_icon(source_icon(source), 24);
    gtk_widget_add_css_class(icon, "source-icon");
    gtk_box_append(GTK_BOX(header), icon);

    GtkWidget *identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(identity, true);
    gtk_box_append(
        GTK_BOX(identity),
        make_label(source.name.c_str(), "source-name"));

    std::string type =
        std::string(source_kind_name(source.kind));
    if (!source.scope.empty()) {
        type += "  •  " + source.scope;
    }
    gtk_box_append(
        GTK_BOX(identity),
        make_label(type.c_str(), "source-meta"));
    gtk_box_append(GTK_BOX(header), identity);

    const bool mutable_source =
        source.kind == infiltrator::software::SourceKind::flatpak ||
        (source.kind == infiltrator::software::SourceKind::apt &&
         !source.backing_file.empty() &&
         source.entry_index != 0U);

    GtkWidget *source_state = nullptr;
    if (mutable_source && state != nullptr &&
        state->window != nullptr) {
        source_state =
            gtk_button_new_with_label(
                source.enabled ? "Enabled" : "Disabled");
        gtk_widget_add_css_class(
            source_state, "source-state-toggle");
        gtk_widget_add_css_class(
            source_state,
            source.enabled
                ? "state-installed"
                : "state-available");
        gtk_widget_set_tooltip_text(
            source_state,
            source.enabled
                ? "Click to disable this source"
                : "Click to enable this source");

        auto *context = new SourceToggleContext{
            state->window,
            source};
        g_object_set_data_full(
            G_OBJECT(source_state),
            "source-toggle-context",
            context,
            destroy_source_toggle_context);
        g_signal_connect(
            source_state,
            "clicked",
            G_CALLBACK(source_toggle_clicked),
            context);
    } else {
        source_state = make_label(
            source.enabled ? "Enabled" : "Disabled",
            source.enabled
                ? "state-installed"
                : "state-available");
        if (source.kind ==
            infiltrator::software::SourceKind::infiltrator) {
            gtk_widget_set_tooltip_text(
                source_state,
                "The built-in Infiltrator project catalogue is always enabled.");
        }
    }

    gtk_box_append(GTK_BOX(header), source_state);
    gtk_box_append(GTK_BOX(card), header);

    GtkWidget *location =
        make_label(source.location.c_str(), "source-location");
    gtk_label_set_wrap(GTK_LABEL(location), true);
    gtk_box_append(GTK_BOX(card), location);

    if (!source.detail.empty()) {
        GtkWidget *detail =
            make_label(source.detail.c_str(), "source-detail");
        gtk_label_set_wrap(GTK_LABEL(detail), true);
        gtk_box_append(GTK_BOX(card), detail);
    }

    if (!source.backing_file.empty()) {
        GtkWidget *file =
            make_label(source.backing_file.c_str(), "source-file");
        gtk_label_set_ellipsize(
            GTK_LABEL(file), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_append(GTK_BOX(card), file);
    }

    return card;
}

void repository_controller_changed(gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr) return;

    rebuild_discover_repository_preview(state);
    if (state->discover_repositories_summary != nullptr) {
        const std::string count =
            std::to_string(state->repositories.records.size());
        gtk_label_set_text(
            GTK_LABEL(state->discover_repositories_summary),
            count.c_str());
    }
}

GtkWidget *repository_make_card(
    gpointer user_data,
    const SourceRecord &source)
{
    return make_source_card(
        static_cast<WindowState *>(user_data),
        source);
}

void refresh_repositories(WindowState *state)
{
    if (state == nullptr) return;
    refresh_repository_controller(&state->repositories);
}

void repository_mirror_settings_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *state =
        static_cast<WindowState *>(
            user_data);
    GError *error = nullptr;
    if (!g_spawn_command_line_async(
            "pkexec mintsources",
            &error)) {
        if (state != nullptr &&
            state->repositories.status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(
                    state->repositories.status),
                error != nullptr &&
                error->message != nullptr
                    ? error->message
                    : "Linux Mint mirror settings are unavailable.");
        }
    } else if (
        state != nullptr &&
        state->repositories.status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(
                state->repositories.status),
            "Mirror settings opened. Refresh repositories after changing a mirror.");
    }
    g_clear_error(&error);
}

GtkWidget *make_repositories_page(WindowState *state)
{
    GtkWidget *page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-repositories");

    GtkWidget *header =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *intro =
        make_page_intro(
            "network-workgroup-symbolic",
            "Repositories",
            "Software sources feeding Discover.");
    gtk_widget_set_hexpand(intro, true);
    gtk_box_append(GTK_BOX(header), intro);

    GtkWidget *add_source =
        gtk_button_new_with_label("Add Source…");
    gtk_widget_add_css_class(add_source, "suggested-action");
    gtk_widget_set_valign(add_source, GTK_ALIGN_CENTER);
    g_signal_connect(
        add_source,
        "clicked",
        G_CALLBACK(add_source_clicked),
        state);
    gtk_box_append(GTK_BOX(header), add_source);

    GtkWidget *mirrors =
        gtk_button_new_with_label(
            "Mint mirrors…");
    gtk_widget_add_css_class(
        mirrors, "control-button");
    gtk_widget_set_valign(
        mirrors, GTK_ALIGN_CENTER);
    g_signal_connect(
        mirrors,
        "clicked",
        G_CALLBACK(
            repository_mirror_settings_clicked),
        state);
    gtk_box_append(
        GTK_BOX(header), mirrors);

    gtk_box_append(GTK_BOX(page), header);

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "SOURCES", "0", "stat-info",
            &state->repositories.count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "APT", "System sources", "stat-operation"),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "FLATPAK", "User + system", "stat-success"),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    state->repositories.status = make_label(
        "Reading configured software sources…",
        "discover-status");
    gtk_box_append(
        GTK_BOX(page), state->repositories.status);

    state->repositories.flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(
        GTK_FLOW_BOX(state->repositories.flow),
        GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(
        GTK_FLOW_BOX(state->repositories.flow), 10U);
    gtk_flow_box_set_column_spacing(
        GTK_FLOW_BOX(state->repositories.flow), 10U);
    gtk_flow_box_set_min_children_per_line(
        GTK_FLOW_BOX(state->repositories.flow), 1U);
    gtk_flow_box_set_max_children_per_line(
        GTK_FLOW_BOX(state->repositories.flow), 2U);
    gtk_widget_set_valign(
        state->repositories.flow, GTK_ALIGN_START);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        state->repositories.flow);
    gtk_box_append(GTK_BOX(page), scroll);

    configure_repository_controller(
        &state->repositories,
        state->window,
        state->repositories.flow,
        state->repositories.count,
        state->repositories.status,
        repository_make_card,
        repository_controller_changed,
        state);

    return page;
}



} // namespace infiltrator::software::app
