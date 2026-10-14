#!/usr/bin/env python3
from pathlib import Path

path = Path("src/app/system_view.cpp")
text = path.read_text()
old = '''void system_snapshots_clicked(
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
        state->system.system_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->system.system_status),
            error != nullptr &&
            error->message != nullptr
                ? error->message
                : "Timeshift is not installed.");
    }
    g_clear_error(&error);
}
'''
new = '''void system_snapshots_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *state =
        static_cast<WindowState *>(
            user_data);
    std::string error;
    GSubprocess *process = start_subprocess(
        {"pkexec", "timeshift-gtk"},
        G_SUBPROCESS_FLAGS_NONE,
        error);
    if (process != nullptr) {
        /* Timeshift owns the interactive snapshot session after launch. */
        g_object_unref(process);
        return;
    }

    if (state != nullptr &&
        state->system.system_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->system.system_status),
            error.empty()
                ? "Timeshift is not installed."
                : error.c_str());
    }
}
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one Timeshift launcher, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
