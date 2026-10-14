#!/usr/bin/env python3
from pathlib import Path

path = Path("src/app/updates_view.cpp")
text = path.read_text()
old = '''    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    std::vector<std::string> candidates;
    if (desktop != nullptr &&
        (std::string_view(desktop) == "Cinnamon" ||
         std::string_view(desktop) == "X-Cinnamon")) {
        candidates.emplace_back(
            "cinnamon-session-quit --reboot");
    } else if (desktop != nullptr &&
               std::string_view(desktop).find("XFCE") !=
                   std::string_view::npos) {
        candidates.emplace_back(
            "xfce4-session-logout --reboot");
    }
    candidates.emplace_back("systemctl reboot");

    GError *last_error = nullptr;
    for (const std::string &command : candidates) {
        GError *error = nullptr;
        if (g_spawn_command_line_async(
                command.c_str(), &error)) {
            g_clear_error(&last_error);
            return;
        }
        g_clear_error(&last_error);
        last_error = error;
    }

    if (state->updates.updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_status),
            last_error != nullptr &&
                    last_error->message != nullptr
                ? last_error->message
                : "Unable to request a system restart.");
    }
    g_clear_error(&last_error);
'''
new = '''    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    std::vector<std::vector<std::string>> candidates;
    if (desktop != nullptr &&
        (std::string_view(desktop) == "Cinnamon" ||
         std::string_view(desktop) == "X-Cinnamon")) {
        candidates.push_back(
            {"cinnamon-session-quit", "--reboot"});
    } else if (desktop != nullptr &&
               std::string_view(desktop).find("XFCE") !=
                   std::string_view::npos) {
        candidates.push_back(
            {"xfce4-session-logout", "--reboot"});
    }
    candidates.push_back({"systemctl", "reboot"});

    std::string last_error;
    for (const std::vector<std::string> &command : candidates) {
        std::string error;
        GSubprocess *process = start_subprocess(
            command,
            G_SUBPROCESS_FLAGS_NONE,
            error);
        if (process != nullptr) {
            /* The desktop/session manager owns restart after launch. */
            g_object_unref(process);
            return;
        }
        if (!error.empty()) {
            last_error = std::move(error);
        }
    }

    if (state->updates.updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_status),
            last_error.empty()
                ? "Unable to request a system restart."
                : last_error.c_str());
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one restart launcher, found {text.count(old)}")
updated = text.replace(old, new, 1)
if "g_spawn_" in updated:
    raise SystemExit("updates_view.cpp still contains a direct g_spawn entry point")
path.write_text(updated)
