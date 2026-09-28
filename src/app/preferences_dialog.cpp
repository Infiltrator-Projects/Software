// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/preferences_dialog.hpp"

#include "app/ui_components.hpp"
#include "app/window_state.hpp"
#include "core/update_policy.hpp"

#include <infiltratr/core.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef INFILTRATOR_SOFTWARE_VERSION
#define INFILTRATOR_SOFTWARE_VERSION "0.0.0"
#endif

#ifndef INFILTRATOR_SOFTWARE_BUILD_PROFILE
#define INFILTRATOR_SOFTWARE_BUILD_PROFILE "cmake"
#endif

namespace infiltrator::software::app {

using infiltrator::software::SoftwarePreferences;
using infiltrator::software::load_software_preferences;
using infiltrator::software::make_label;
using infiltrator::software::save_software_preferences;

void refresh_updates(WindowState *state, bool refresh_metadata);
std::string one_line(std::string value);

struct PreferencesDialogContext {
    WindowState *state{};
    GtkWindow *window{};
    GtkWidget *refresh_enabled{};
    GtkWidget *first_refresh{};
    GtkWidget *recurring_refresh{};
    GtkWidget *notifications{};
    GtkWidget *security_notifications{};
    GtkWidget *notify_max_days{};
    GtkWidget *notify_max_age{};
    GtkWidget *notify_grace{};
    GtkWidget *notify_between{};
    GtkWidget *show_flatpak{};
    GtkWidget *show_cinnamon{};
    GtkWidget *auto_packages{};
    GtkWidget *auto_flatpaks{};
    GtkWidget *auto_cinnamon{};
    GtkWidget *auto_maintenance{};
    GtkWidget *hide_after{};
    GtkWidget *hide_tray{};
    GtkWidget *install_recommends{};
    GtkWidget *keep_configuration{};
    GtkWidget *snapshot_before{};
    GtkWidget *ignored{};
    GtkWidget *status{};
};

void destroy_preferences_context(gpointer data)
{
    delete static_cast<PreferencesDialogContext *>(data);
}

GtkWidget *preference_check(
    const char *label,
    const bool active)
{
    GtkWidget *button =
        gtk_check_button_new_with_label(label);
    gtk_check_button_set_active(
        GTK_CHECK_BUTTON(button), active);
    return button;
}

GtkWidget *preference_spin_row(
    const char *label,
    const unsigned value,
    GtkWidget **out)
{
    GtkWidget *row =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *copy =
        make_label(label, "preferences-copy");
    gtk_widget_set_hexpand(copy, true);

    GtkWidget *spin =
        gtk_spin_button_new_with_range(
            1.0, 10080.0, 1.0);
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(spin),
        static_cast<double>(value));
    gtk_box_append(GTK_BOX(row), copy);
    gtk_box_append(GTK_BOX(row), spin);

    if (out != nullptr) {
        *out = spin;
    }
    return row;
}

struct PreferencesSaveOperation {
    GtkWindow *preferences_window{};
    GtkWindow *main_window{};
    SoftwarePreferences previous;
    SoftwarePreferences next;
};

void destroy_preferences_save_operation(
    PreferencesSaveOperation *operation)
{
    if (operation == nullptr) {
        return;
    }
    if (operation->preferences_window != nullptr) {
        g_object_unref(operation->preferences_window);
    }
    if (operation->main_window != nullptr) {
        g_object_unref(operation->main_window);
    }
    delete operation;
}

std::vector<std::string> automation_preferences_arguments(
    const SoftwarePreferences &preferences)
{
    std::vector<std::string> arguments{
        "pkexec",
        "/usr/libexec/infiltrator-software-update-helper",
        "configure-automation",
        std::string("auto-update-packages=") +
            (preferences.auto_update_packages ? "true" : "false"),
        std::string("auto-remove-obsolete=") +
            (preferences.auto_remove_obsolete ? "true" : "false"),
        "first-refresh-minutes=" +
            std::to_string(preferences.first_refresh_minutes),
        "recurring-refresh-minutes=" +
            std::to_string(preferences.recurring_refresh_minutes),
        std::string("install-recommends=") +
            (preferences.install_recommends ? "true" : "false"),
        std::string("keep-configuration=") +
            (preferences.keep_configuration ? "true" : "false"),
        std::string("snapshot-before-system-updates=") +
            (preferences.snapshot_before_system_updates ? "true" : "false")
    };
    for (const std::string &rule :
         preferences.ignored_packages) {
        arguments.push_back("ignore=" + rule);
    }
    return arguments;
}

void preferences_automation_sync_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *operation =
        static_cast<PreferencesSaveOperation *>(
            user_data);
    auto *process = G_SUBPROCESS(source_object);

    GError *gerror = nullptr;
    gchar *out = nullptr;
    gchar *err = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process,
            async_result,
            &out,
            &err,
            &gerror);
    const bool success =
        communicated != FALSE &&
        g_subprocess_get_successful(process);

    auto *context =
        operation == nullptr ||
        operation->preferences_window == nullptr
            ? nullptr
            : static_cast<PreferencesDialogContext *>(
                  g_object_get_data(
                      G_OBJECT(operation->preferences_window),
                      "software-preferences-context"));
    auto *state =
        operation == nullptr ||
        operation->main_window == nullptr
            ? nullptr
            : static_cast<WindowState *>(
                  g_object_get_data(
                      G_OBJECT(operation->main_window),
                      "infiltrator-window-state"));

    if (success) {
        if (state != nullptr) {
            state->preferences =
                operation->next;
        }
        if (operation != nullptr &&
            operation->preferences_window != nullptr) {
            gtk_window_destroy(
                operation->preferences_window);
        }
        if (state != nullptr) {
            refresh_updates(state, false);
        }
    } else {
        std::string message;
        if (err != nullptr && *err != '\0') {
            message = one_line(err);
        } else if (
            gerror != nullptr &&
            gerror->message != nullptr) {
            message = gerror->message;
        } else {
            message =
                "Automatic system-update settings were not applied.";
        }

        std::string rollback_error;
        const bool rolled_back =
            operation != nullptr &&
            save_software_preferences(
                operation->previous,
                rollback_error);
        if (!rolled_back) {
            message +=
                " The previous user preferences could not be restored: " +
                rollback_error;
        }

        if (context != nullptr &&
            context->status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(context->status),
                message.c_str());
        }
        if (operation != nullptr &&
            operation->preferences_window != nullptr) {
            gtk_widget_set_sensitive(
                GTK_WIDGET(operation->preferences_window),
                true);
        }
    }

    g_free(out);
    g_free(err);
    g_clear_error(&gerror);
    destroy_preferences_save_operation(operation);
}

bool start_preferences_automation_sync(
    PreferencesDialogContext *context,
    const SoftwarePreferences &previous,
    const SoftwarePreferences &next,
    std::string &error)
{
    if (context == nullptr ||
        context->window == nullptr ||
        context->state == nullptr ||
        context->state->window == nullptr) {
        error =
            "Software Preferences window state is unavailable.";
        return false;
    }

    std::vector<std::string> arguments =
        automation_preferences_arguments(next);
    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    GError *gerror = nullptr;
    GSubprocess *process =
        g_subprocess_newv(
            argv.data(),
            static_cast<GSubprocessFlags>(
                G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                G_SUBPROCESS_FLAGS_STDERR_PIPE),
            &gerror);
    if (process == nullptr) {
        error =
            gerror != nullptr &&
            gerror->message != nullptr
                ? gerror->message
                : "Unable to start the automatic-update configuration helper.";
        g_clear_error(&gerror);
        return false;
    }

    auto *operation =
        new PreferencesSaveOperation{
            GTK_WINDOW(g_object_ref(context->window)),
            GTK_WINDOW(g_object_ref(context->state->window)),
            previous,
            next};

    gtk_widget_set_sensitive(
        GTK_WIDGET(context->window), false);
    if (context->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(context->status),
            "Applying automatic-update settings…");
    }

    g_subprocess_communicate_utf8_async(
        process,
        nullptr,
        nullptr,
        preferences_automation_sync_complete,
        operation);
    g_object_unref(process);
    error.clear();
    return true;
}

void preferences_save(GtkButton *, gpointer user_data)
{
    auto *context =
        static_cast<PreferencesDialogContext *>(user_data);
    if (context == nullptr ||
        context->state == nullptr) {
        return;
    }

    SoftwarePreferences preferences =
        context->state->preferences;

    preferences.refresh_schedule_enabled =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->refresh_enabled));
    preferences.first_refresh_minutes =
        static_cast<unsigned>(
            gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(context->first_refresh)));
    preferences.recurring_refresh_minutes =
        static_cast<unsigned>(
            gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(context->recurring_refresh)));

    preferences.notifications_enabled =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->notifications));
    preferences.notifications_security_only =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->security_notifications));
    preferences.notify_max_days =
        static_cast<unsigned>(
            gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(context->notify_max_days)));
    preferences.notify_max_age_days =
        static_cast<unsigned>(
            gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(context->notify_max_age)));
    preferences.notify_grace_period_days =
        static_cast<unsigned>(
            gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(context->notify_grace)));
    preferences.notify_days_between =
        static_cast<unsigned>(
            gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(context->notify_between)));

    preferences.show_flatpak_updates =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->show_flatpak));
    preferences.show_cinnamon_updates =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->show_cinnamon));

    preferences.auto_update_packages =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->auto_packages));
    preferences.auto_update_flatpaks =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->auto_flatpaks));
    preferences.auto_update_cinnamon_spices =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->auto_cinnamon));
    preferences.auto_remove_obsolete =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->auto_maintenance));

    preferences.hide_window_after_update =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->hide_after));
    preferences.hide_tray =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->hide_tray));
    preferences.install_recommends =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->install_recommends));
    preferences.keep_configuration =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->keep_configuration));
    preferences.snapshot_before_system_updates =
        gtk_check_button_get_active(
            GTK_CHECK_BUTTON(context->snapshot_before));

    preferences.ignored_packages.clear();
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(context->ignored));
    GtkTextIter begin;
    GtkTextIter finish;
    gtk_text_buffer_get_bounds(
        buffer, &begin, &finish);
    gchar *ignored =
        gtk_text_buffer_get_text(
            buffer, &begin, &finish, false);
    if (ignored != nullptr) {
        std::istringstream input(ignored);
        std::string line;
        while (std::getline(input, line)) {
            const auto first =
                std::find_if(
                    line.begin(),
                    line.end(),
                    [](const unsigned char ch) {
                        return std::isspace(ch) == 0;
                    });
            line.erase(line.begin(), first);
            while (!line.empty() &&
                   std::isspace(
                       static_cast<unsigned char>(
                           line.back())) != 0) {
                line.pop_back();
            }
            if (!line.empty()) {
                preferences.ignored_packages.push_back(line);
            }
        }
    }
    g_free(ignored);

    const bool automation_changed =
        preferences.auto_update_packages !=
            context->state->preferences.auto_update_packages ||
        preferences.auto_remove_obsolete !=
            context->state->preferences.auto_remove_obsolete ||
        preferences.first_refresh_minutes !=
            context->state->preferences.first_refresh_minutes ||
        preferences.recurring_refresh_minutes !=
            context->state->preferences.recurring_refresh_minutes ||
        preferences.install_recommends !=
            context->state->preferences.install_recommends ||
        preferences.keep_configuration !=
            context->state->preferences.keep_configuration ||
        preferences.snapshot_before_system_updates !=
            context->state->preferences.snapshot_before_system_updates ||
        preferences.ignored_packages !=
            context->state->preferences.ignored_packages;

    const SoftwarePreferences previous =
        context->state->preferences;
    std::string error;
    if (!save_software_preferences(
            preferences, error)) {
        gtk_label_set_text(
            GTK_LABEL(context->status),
            error.c_str());
        return;
    }

    if (automation_changed) {
        if (!start_preferences_automation_sync(
                context,
                previous,
                preferences,
                error)) {
            std::string rollback_error;
            const bool rolled_back =
                save_software_preferences(
                    previous,
                    rollback_error);
            std::string message = error;
            if (!rolled_back) {
                message +=
                    " The previous user preferences could not be restored: " +
                    rollback_error;
            }
            gtk_label_set_text(
                GTK_LABEL(context->status),
                message.c_str());
        }
        return;
    }

    context->state->preferences =
        std::move(preferences);
    gtk_window_destroy(context->window);
    refresh_updates(context->state, false);
}

void settings_clicked(GtkButton *, gpointer user_data)
{
    auto *state =
        static_cast<WindowState *>(user_data);
    if (state == nullptr ||
        state->window == nullptr) {
        return;
    }

    std::string preference_error;
    SoftwarePreferences loaded_preferences =
        state->preferences;
    if (load_software_preferences(
            loaded_preferences,
            preference_error)) {
        state->preferences =
            std::move(loaded_preferences);
    }

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(
        GTK_WINDOW(window),
        "Software Preferences");
    gtk_window_set_transient_for(
        GTK_WINDOW(window),
        state->window);
    gtk_window_set_modal(
        GTK_WINDOW(window), true);
    gtk_window_set_destroy_with_parent(
        GTK_WINDOW(window), true);
    gtk_window_set_default_size(
        GTK_WINDOW(window), 620, 720);

    auto *context =
        new PreferencesDialogContext{};
    context->state = state;
    context->window = GTK_WINDOW(window);
    g_object_set_data_full(
        G_OBJECT(window),
        "software-preferences-context",
        context,
        destroy_preferences_context);

    GtkWidget *content =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(
        content, "preferences-page");
    gtk_widget_set_margin_top(content, 18);
    gtk_widget_set_margin_bottom(content, 18);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);

    gtk_box_append(
        GTK_BOX(content),
        make_label(
            "Software Preferences",
            "preferences-title"));
    GtkWidget *intro =
        make_label(
            "Update scheduling, notifications, automation, compatibility policy and ignored updates.",
            "preferences-copy");
    gtk_label_set_wrap(GTK_LABEL(intro), true);
    gtk_box_append(GTK_BOX(content), intro);

    GtkWidget *scroll =
        gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    GtkWidget *options =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 10);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        options);

    gtk_box_append(
        GTK_BOX(options),
        make_label("Appearance", "card-title"));
    gtk_box_append(
        GTK_BOX(options),
        state->theme.create_selector());

    gtk_box_append(
        GTK_BOX(options),
        make_label("Refresh schedule", "card-title"));
    context->refresh_enabled =
        preference_check(
            "Enable scheduled repository refresh",
            state->preferences.refresh_schedule_enabled);
    gtk_box_append(
        GTK_BOX(options),
        context->refresh_enabled);
    gtk_box_append(
        GTK_BOX(options),
        preference_spin_row(
            "First refresh after startup (minutes)",
            state->preferences.first_refresh_minutes,
            &context->first_refresh));
    gtk_box_append(
        GTK_BOX(options),
        preference_spin_row(
            "Recurring refresh interval (minutes)",
            state->preferences.recurring_refresh_minutes,
            &context->recurring_refresh));

    gtk_box_append(
        GTK_BOX(options),
        make_label("Notifications", "card-title"));
    context->notifications =
        preference_check(
            "Notify when updates remain outstanding",
            state->preferences.notifications_enabled);
    context->security_notifications =
        preference_check(
            "Age notifications from security and kernel updates only",
            state->preferences.notifications_security_only);
    gtk_box_append(
        GTK_BOX(options),
        context->notifications);
    gtk_box_append(
        GTK_BOX(options),
        context->security_notifications);
    gtk_box_append(
        GTK_BOX(options),
        preference_spin_row(
            "Notify after an update has remained available for logged-in days",
            state->preferences.notify_max_days,
            &context->notify_max_days));
    gtk_box_append(
        GTK_BOX(options),
        preference_spin_row(
            "Notify when an update is older than days",
            state->preferences.notify_max_age_days,
            &context->notify_max_age));
    gtk_box_append(
        GTK_BOX(options),
        preference_spin_row(
            "Suppress notifications after a recent update for days",
            state->preferences.notify_grace_period_days,
            &context->notify_grace));
    gtk_box_append(
        GTK_BOX(options),
        preference_spin_row(
            "Minimum days between repeated notifications",
            state->preferences.notify_days_between,
            &context->notify_between));

    gtk_box_append(
        GTK_BOX(options),
        make_label("Update sources", "card-title"));
    context->show_flatpak =
        preference_check(
            "Show Flatpak updates",
            state->preferences.show_flatpak_updates);
    context->show_cinnamon =
        preference_check(
            "Show Cinnamon Spice updates",
            state->preferences.show_cinnamon_updates);
    gtk_box_append(GTK_BOX(options), context->show_flatpak);
    gtk_box_append(GTK_BOX(options), context->show_cinnamon);

    gtk_box_append(
        GTK_BOX(options),
        make_label("Automatic updates", "card-title"));
    context->auto_packages =
        preference_check(
            "Automatically install system package updates",
            state->preferences.auto_update_packages);
    context->auto_flatpaks =
        preference_check(
            "Automatically update Flatpaks",
            state->preferences.auto_update_flatpaks);
    context->auto_cinnamon =
        preference_check(
            "Automatically update Cinnamon Spices",
            state->preferences.auto_update_cinnamon_spices);
    context->auto_maintenance =
        preference_check(
            "Weekly: remove obsolete kernels and unused dependencies",
            state->preferences.auto_remove_obsolete);
    gtk_widget_set_tooltip_text(
        context->auto_maintenance,
        "Runs a simulated Debian autoremove first, refuses to remove the running kernel or Software's retained fallback, then applies maintenance under shutdown/sleep inhibition.");
    gtk_box_append(GTK_BOX(options), context->auto_packages);
    gtk_box_append(GTK_BOX(options), context->auto_flatpaks);
    gtk_box_append(GTK_BOX(options), context->auto_cinnamon);

    gtk_box_append(
        GTK_BOX(options),
        make_label("Automatic maintenance", "card-title"));
    gtk_box_append(
        GTK_BOX(options),
        context->auto_maintenance);

    gtk_box_append(
        GTK_BOX(options),
        make_label("Behaviour", "card-title"));
    context->hide_after =
        preference_check(
            "Hide Software after a successful update",
            state->preferences.hide_window_after_update);
    context->hide_tray =
        preference_check(
            "Hide the update indicator when up to date",
            state->preferences.hide_tray);
    context->install_recommends =
        preference_check(
            "Install recommended packages when requested",
            state->preferences.install_recommends);
    context->keep_configuration =
        preference_check(
            "Keep locally modified configuration files",
            state->preferences.keep_configuration);
    context->snapshot_before =
        preference_check(
            "Offer a snapshot before system-critical updates",
            state->preferences.snapshot_before_system_updates);
    gtk_box_append(GTK_BOX(options), context->hide_after);
    gtk_box_append(GTK_BOX(options), context->hide_tray);
    gtk_box_append(GTK_BOX(options), context->install_recommends);
    gtk_box_append(GTK_BOX(options), context->keep_configuration);
    gtk_box_append(GTK_BOX(options), context->snapshot_before);

    gtk_box_append(
        GTK_BOX(options),
        make_label("Ignored updates", "card-title"));
    GtkWidget *ignored_help =
        make_label(
            "One source-package pattern per line. Wildcards are supported. Use package=VERSION to ignore only one version.",
            "preferences-copy");
    gtk_label_set_wrap(
        GTK_LABEL(ignored_help), true);
    gtk_box_append(
        GTK_BOX(options), ignored_help);

    context->ignored =
        gtk_text_view_new();
    gtk_widget_set_size_request(
        context->ignored, -1, 120);
    GtkTextBuffer *ignored_buffer =
        gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(context->ignored));
    std::string ignored_text;
    for (const std::string &rule :
         state->preferences.ignored_packages) {
        ignored_text += rule;
        ignored_text += '\n';
    }
    gtk_text_buffer_set_text(
        ignored_buffer,
        ignored_text.c_str(),
        -1);
    gtk_box_append(
        GTK_BOX(options),
        context->ignored);

    gtk_box_append(
        GTK_BOX(content), scroll);

    context->status =
        make_label(
            preference_error.c_str(),
            "preferences-copy");
    gtk_box_append(
        GTK_BOX(content), context->status);

    GtkWidget *actions =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(
        actions, GTK_ALIGN_END);

    GtkWidget *about =
        gtk_button_new_with_label(
            "About Software");
    g_signal_connect(
        about,
        "clicked",
        G_CALLBACK(about_clicked),
        state);
    gtk_box_append(
        GTK_BOX(actions), about);

    GtkWidget *save =
        gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(
        save, "suggested-action");
    g_signal_connect(
        save,
        "clicked",
        G_CALLBACK(preferences_save),
        context);
    gtk_box_append(
        GTK_BOX(actions), save);

    gtk_box_append(
        GTK_BOX(content), actions);

    gtk_window_set_child(
        GTK_WINDOW(window), content);
    gtk_window_present(
        GTK_WINDOW(window));
}

void about_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr) {
        return;
    }

    GtkWidget *dialog = gtk_about_dialog_new();
    const char *profile = INFILTRATOR_SOFTWARE_BUILD_PROFILE;
    char comments[512];
    std::snprintf(
        comments, sizeof(comments),
        "Unified software management and updates for the Infiltrator project family.\n\nBuild: %s",
        infiltratr_build_profile_label(profile));

    gtk_about_dialog_set_program_name(
        GTK_ABOUT_DIALOG(dialog), "Infiltrator Software");
    gtk_about_dialog_set_logo_icon_name(
        GTK_ABOUT_DIALOG(dialog), "net.ssmith.infiltrator.software");
    gtk_window_set_icon_name(GTK_WINDOW(dialog), "net.ssmith.infiltrator.software");
    gtk_about_dialog_set_version(
        GTK_ABOUT_DIALOG(dialog), INFILTRATOR_SOFTWARE_VERSION);
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), comments);
    gtk_about_dialog_set_website(
        GTK_ABOUT_DIALOG(dialog),
        "https://github.com/Infiltrator-Projects/Software");
    gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dialog), "Website");
    gtk_about_dialog_set_copyright(
        GTK_ABOUT_DIALOG(dialog), "Copyright © 2026 Shannon Smith");
    gtk_about_dialog_set_license_type(
        GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_CUSTOM);
    gtk_about_dialog_set_license(
        GTK_ABOUT_DIALOG(dialog),
        "Infiltrator Software is free software licensed under the GNU General "
        "Public License version 3 or, at your option, any later version "
        "(GPL-3.0-or-later).\n\n"
        "See LICENSE in the source package for the complete licence text.");
    gtk_about_dialog_set_wrap_license(GTK_ABOUT_DIALOG(dialog), true);

    static const char *authors[] = {
        "Shannon Smith — Author and project maintainer",
        nullptr
    };
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);

    gtk_window_set_transient_for(GTK_WINDOW(dialog), state->window);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), true);
    gtk_window_set_modal(GTK_WINDOW(dialog), true);
    gtk_window_present(GTK_WINDOW(dialog));
}



} // namespace infiltrator::software::app
