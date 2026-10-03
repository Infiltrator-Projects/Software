from pathlib import Path

tray_path = Path("src/tray/main.cpp")
tray = tray_path.read_text()

old = """    bool replacement_exec_failed{false};
};
"""
new = """    bool replacement_exec_failed{false};
    GFileMonitor *preferences_monitor{};
    GFileMonitor *override_monitor{};
    GFileMonitor *executable_monitor{};
};
"""
assert old in tray
tray = tray.replace(old, new, 1)

start = tray.index("gboolean state_tick(gpointer user_data)\n{")
end = tray.index("\ngboolean clear_opening_software(gpointer user_data)", start)
replacement = r'''bool monitored_event_matches(
    GFile *file,
    GFile *other_file,
    const std::filesystem::path &target)
{
    const auto matches =
        [&](GFile *candidate) {
            if (candidate == nullptr) {
                return false;
            }
            gchar *raw_path = g_file_get_path(candidate);
            if (raw_path == nullptr) {
                return false;
            }
            const bool same =
                std::filesystem::path(raw_path) == target;
            g_free(raw_path);
            return same;
        };
    return matches(file) || matches(other_file);
}

void replace_running_tray_if_needed(TrayState *state)
{
    if (state == nullptr || state->replacement_exec_failed ||
        !installed_tray_replaced()) {
        return;
    }

    /*
     * dpkg replaces the executable atomically. React to the directory event
     * instead of polling /proc and /usr/bin every two seconds for the entire
     * desktop session. The lock fd is O_CLOEXEC, so the replacement image
     * reacquires the normal single-instance lock.
     */
    (void)execl(
        kInstalledTrayPath,
        "infiltrator-software-tray",
        "--replace",
        static_cast<char *>(nullptr));
    state->replacement_exec_failed = true;
    g_warning(
        "Unable to replace stale Software tray executable: %s",
        g_strerror(errno));
}

void runtime_file_changed(
    GFileMonitor *,
    GFile *file,
    GFile *other_file,
    GFileMonitorEvent,
    gpointer user_data)
{
    auto *state = static_cast<TrayState *>(user_data);
    if (state == nullptr) {
        return;
    }

    const std::filesystem::path preferences =
        software_preferences_path();
    const std::filesystem::path override = state_file();
    if (!monitored_event_matches(file, other_file, preferences) &&
        !monitored_event_matches(file, other_file, override)) {
        return;
    }

    if (refresh_runtime_inputs(state)) {
        render(state);
    }
}

void installed_executable_changed(
    GFileMonitor *,
    GFile *file,
    GFile *other_file,
    GFileMonitorEvent,
    gpointer user_data)
{
    if (!monitored_event_matches(
            file,
            other_file,
            std::filesystem::path(kInstalledTrayPath))) {
        return;
    }
    replace_running_tray_if_needed(
        static_cast<TrayState *>(user_data));
}

GFileMonitor *monitor_parent_directory(
    const std::filesystem::path &target,
    GCallback callback,
    TrayState *state)
{
    if (target.empty() || target.parent_path().empty()) {
        return nullptr;
    }

    GFile *directory =
        g_file_new_for_path(target.parent_path().c_str());
    if (directory == nullptr) {
        return nullptr;
    }

    GError *error = nullptr;
    GFileMonitor *monitor =
        g_file_monitor_directory(
            directory,
            G_FILE_MONITOR_NONE,
            nullptr,
            &error);
    g_object_unref(directory);
    if (monitor == nullptr) {
        if (error != nullptr) {
            g_debug(
                "Unable to monitor %s: %s",
                target.parent_path().c_str(),
                error->message);
            g_error_free(error);
        }
        return nullptr;
    }

    g_signal_connect(monitor, "changed", callback, state);
    return monitor;
}

void install_file_monitors(TrayState *state)
{
    if (state == nullptr) {
        return;
    }

    state->preferences_monitor =
        monitor_parent_directory(
            software_preferences_path(),
            G_CALLBACK(runtime_file_changed),
            state);
    state->override_monitor =
        monitor_parent_directory(
            state_file(),
            G_CALLBACK(runtime_file_changed),
            state);
    state->executable_monitor =
        monitor_parent_directory(
            std::filesystem::path(kInstalledTrayPath),
            G_CALLBACK(installed_executable_changed),
            state);
}
'''
tray = tray[:start] + replacement + tray[end:]

old = """    subscribe_engine(&state);
    render(&state);
"""
new = """    subscribe_engine(&state);
    install_file_monitors(&state);
    render(&state);
"""
assert old in tray
tray = tray.replace(old, new, 1)

old = """    g_timeout_add_seconds(60U, scheduled_check, &state);
    g_timeout_add_seconds(2U, state_tick, &state);

    gtk_main();
"""
new = """    g_timeout_add_seconds(60U, scheduled_check, &state);

    gtk_main();
"""
assert old in tray
tray = tray.replace(old, new, 1)

old = """    if (state.engine_connection != nullptr) {
"""
new = """    if (state.preferences_monitor != nullptr) {
        g_object_unref(state.preferences_monitor);
    }
    if (state.override_monitor != nullptr) {
        g_object_unref(state.override_monitor);
    }
    if (state.executable_monitor != nullptr) {
        g_object_unref(state.executable_monitor);
    }
    if (state.engine_connection != nullptr) {
"""
assert old in tray
tray = tray.replace(old, new, 1)
assert "g_timeout_add_seconds(2U, state_tick" not in tray
assert "g_file_monitor_directory" in tray
tray_path.write_text(tray)

theme_path = Path("src/app/theme.cpp")
theme = theme_path.read_text()
pairs = [
(
'''        << ".titlebar-brand-icon { background: " << card
        << "; border: 1px solid " << border
        << "; border-radius: 12px; padding: 7px; }"''',
'''        << ".titlebar-brand-icon { background: " << card
        << "; border: 1px solid " << border
        << "; border-radius: " << metrics->card_radius << "px; padding: 7px; }"'''
),
(
'''        << "; color: " << title << "; }"''',
'''        << "; color: " << heading << "; }"'''
),
(
'''        << "; border: 1px solid " << status_border
        << "; border-radius: 14px; padding: 8px 12px; box-shadow: none; }"''',
'''        << "; border: 1px solid " << status_border
        << "; border-radius: " << metrics->control_radius
        << "px; padding: 8px 12px; box-shadow: none; }"'''
),
(
'''        << "padding: 4px; background: transparent; border: 1px solid transparent; "
        << "border-radius: 8px; box-shadow: none; }"''',
'''        << "padding: 4px; background: transparent; border: 1px solid transparent; "
        << "border-radius: " << metrics->small_radius << "px; box-shadow: none; }"'''
),
(
'''        << ".nav-row { min-height: 50px; margin: 2px 4px; padding: 7px 9px; "
        << "border: 1px solid transparent; border-radius: 12px; }"''',
'''        << ".nav-row { min-height: 50px; margin: 2px 4px; padding: 7px 9px; "
        << "border: 1px solid transparent; border-radius: "
        << metrics->card_radius << "px; }"'''
),
(
'''        << "; border-radius: 11px; padding: 5px; }"''',
'''        << "; border-radius: " << metrics->control_radius
        << "px; padding: 5px; }"'''
),
(
'''        << "; border-radius: 12px; box-shadow: none; }"''',
'''        << "; border-radius: " << metrics->card_radius
        << "px; box-shadow: none; }"'''
),
(
'''        << "background: transparent; border-radius: 10px; }"''',
'''        << "background: transparent; border-radius: "
        << metrics->control_radius << "px; }"'''
),
]
for old, new in pairs:
    assert old in theme, old
    theme = theme.replace(old, new, 1)
theme_path.write_text(theme)

contracts_path = Path("tests/source_contracts.py")
contracts = contracts_path.read_text()
old = '''assert "refresh_runtime_inputs" in tray
assert "if (runtime_changed)" in tray
assert "const std::string override = read_override();" not in tray
'''
new = '''assert "refresh_runtime_inputs" in tray
assert "install_file_monitors" in tray
assert "g_file_monitor_directory" in tray
assert "runtime_file_changed" in tray
assert "installed_executable_changed" in tray
assert "g_timeout_add_seconds(2U, state_tick" not in tray
assert "const std::string override = read_override();" not in tray
'''
assert old in contracts
contracts = contracts.replace(old, new, 1)
marker = 'assert ".nav-icon-well" in theme\n'
addition = '''assert ".nav-icon-well" in theme
assert "metrics->control_radius" in theme
assert "metrics->small_radius" in theme
assert "metrics->card_radius" in theme
assert "border-radius: 14px; padding: 8px 12px" not in theme
assert "border-radius: 8px; box-shadow: none" not in theme
assert "border-radius: 11px; padding: 5px" not in theme
'''
assert marker in contracts
contracts = contracts.replace(marker, addition, 1)
contracts_path.write_text(contracts)
