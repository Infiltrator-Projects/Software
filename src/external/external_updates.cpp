// SPDX-License-Identifier: GPL-3.0-or-later
#include "external/external_updates.hpp"
#include "external/cinnamon_spices.hpp"

#include <gio/gio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

std::string trim(const std::string_view value)
{
    std::size_t first = 0U;
    while (first < value.size() &&
           std::isspace(
               static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(
               static_cast<unsigned char>(
                   value[last - 1U])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::vector<std::string_view> split(
    const std::string_view value,
    const char separator)
{
    std::vector<std::string_view> result;
    std::size_t start = 0U;
    for (;;) {
        const std::size_t position =
            value.find(separator, start);
        if (position == std::string_view::npos) {
            result.emplace_back(value.substr(start));
            break;
        }
        result.emplace_back(
            value.substr(start, position - start));
        start = position + 1U;
    }
    return result;
}

bool run_command(
    const std::vector<std::string> &arguments,
    std::string &stdout_text,
    std::string &error)
{
    stdout_text.clear();
    error.clear();

    if (arguments.empty()) {
        error = "External update command is empty.";
        return false;
    }

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
            gerror == nullptr || gerror->message == nullptr
                ? "Unable to start external update command."
                : gerror->message;
        g_clear_error(&gerror);
        return false;
    }

    gchar *out = nullptr;
    gchar *err = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8(
            process,
            nullptr,
            nullptr,
            &out,
            &err,
            &gerror);
    const bool success =
        communicated &&
        g_subprocess_get_successful(process);

    if (out != nullptr) {
        stdout_text = out;
    }
    if (!success) {
        if (err != nullptr && *err != '\0') {
            error = trim(err);
        } else if (
            gerror != nullptr &&
            gerror->message != nullptr) {
            error = gerror->message;
        } else {
            error =
                "External update command returned an error.";
        }
    }

    g_free(out);
    g_free(err);
    g_clear_error(&gerror);
    g_object_unref(process);
    return success;
}


bool run_command_streaming(
    const std::vector<std::string> &arguments,
    std::string &output,
    std::string &error,
    const ExternalProgressCallback &progress)
{
    output.clear();
    error.clear();
    if (arguments.empty()) {
        error = "External update command is empty.";
        return false;
    }

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
                G_SUBPROCESS_FLAGS_STDERR_MERGE),
            &gerror);
    if (process == nullptr) {
        error =
            gerror == nullptr || gerror->message == nullptr
                ? "Unable to start external update command."
                : gerror->message;
        g_clear_error(&gerror);
        return false;
    }

    GInputStream *stream =
        g_subprocess_get_stdout_pipe(process);
    std::array<char, 1024U> buffer{};
    std::string pending;
    for (;;) {
        const gssize bytes =
            g_input_stream_read(
                stream,
                buffer.data(),
                buffer.size(),
                nullptr,
                &gerror);
        if (bytes < 0) {
            error =
                gerror == nullptr || gerror->message == nullptr
                    ? "Unable to read external update progress."
                    : gerror->message;
            g_clear_error(&gerror);
            g_object_unref(process);
            return false;
        }
        if (bytes == 0) {
            break;
        }

        output.append(
            buffer.data(),
            static_cast<std::size_t>(bytes));
        pending.append(
            buffer.data(),
            static_cast<std::size_t>(bytes));
        for (;;) {
            const std::size_t separator =
                pending.find_first_of("\r\n");
            if (separator == std::string::npos) {
                break;
            }
            const std::string line =
                trim(std::string_view(pending).substr(
                    0U, separator));
            pending.erase(0U, separator + 1U);
            while (!pending.empty() &&
                   (pending.front() == '\r' ||
                    pending.front() == '\n')) {
                pending.erase(pending.begin());
            }
            if (progress && !line.empty()) {
                progress(line);
            }
        }
    }

    const std::string last = trim(pending);
    if (progress && !last.empty()) {
        progress(last);
    }

    const gboolean success =
        g_subprocess_wait_check(
            process,
            nullptr,
            &gerror);
    if (!success) {
        if (!last.empty()) {
            error = last;
        } else if (
            gerror != nullptr &&
            gerror->message != nullptr) {
            error = gerror->message;
        } else {
            error = "External update command returned an error.";
        }
    }
    g_clear_error(&gerror);
    g_object_unref(process);
    return success;
}

bool program_available(const char *name)
{
    gchar *path = g_find_program_in_path(name);
    if (path == nullptr) {
        return false;
    }
    g_free(path);
    return true;
}

std::uint64_t parse_size(std::string text)
{
    text = trim(text);
    if (text.empty()) {
        return 0U;
    }

    /*
     * Flatpak may return a localized human-readable size even when asked for
     * download-size. Preserve honest semantics: parse only a bare byte count.
     * The UI otherwise shows an unknown size rather than inventing one.
     */
    std::uint64_t value = 0U;
    const auto parsed =
        std::from_chars(
            text.data(),
            text.data() + text.size(),
            value);
    if (parsed.ec == std::errc{} &&
        parsed.ptr == text.data() + text.size()) {
        return value;
    }
    return 0U;
}

ExternalUpdateKind flatpak_kind(
    const std::string_view ref)
{
    return ref.rfind("runtime/", 0U) == 0U
        ? ExternalUpdateKind::flatpak_runtime
        : ExternalUpdateKind::flatpak_application;
}

void parse_flatpak_output(
    const std::string &output,
    const std::string_view installation,
    std::vector<ExternalUpdate> &updates,
    std::set<std::string> &seen)
{
    std::size_t start = 0U;
    while (start <= output.size()) {
        const std::size_t newline =
            output.find('\n', start);
        const std::size_t end =
            newline == std::string::npos
                ? output.size()
                : newline;
        const std::string line =
            trim(std::string_view(output).substr(
                start, end - start));
        if (!line.empty()) {
            const auto fields = split(line, '\t');
            if (!fields.empty()) {
                ExternalUpdate update;
                update.backend = "Flatpak";
                update.id = trim(fields[0]);
                update.name = update.id;
                if (fields.size() > 1U) {
                    update.detail = trim(fields[1]);
                }
                if (fields.size() > 2U) {
                    update.version = trim(fields[2]);
                }
                if (fields.size() > 3U) {
                    update.download_bytes =
                        parse_size(
                            std::string(fields[3]));
                }

                const std::string ref =
                    update.detail.empty()
                        ? update.id
                        : update.detail;
                update.ref = ref;
                update.user_installation = installation == "User";
                update.kind = flatpak_kind(ref);
                update.detail =
                    std::string(installation) +
                    (ref.empty()
                         ? std::string{}
                         : " • " + ref);

                const std::string key =
                    std::string(installation) +
                    "\n" + update.id +
                    "\n" + ref;
                if (!update.id.empty() &&
                    seen.insert(key).second) {
                    updates.emplace_back(
                        std::move(update));
                }
            }
        }

        if (newline == std::string::npos) {
            break;
        }
        start = newline + 1U;
    }
}

bool discover_flatpak_installation(
    const bool user,
    std::vector<ExternalUpdate> &updates,
    std::set<std::string> &seen,
    std::string &error)
{
    std::vector<std::string> command{
        "flatpak",
        "remote-ls",
        "--updates",
        "--columns=application,ref,version,download-size"
    };
    if (user) {
        command.insert(
            command.begin() + 2,
            "--user");
    } else {
        command.insert(
            command.begin() + 2,
            "--system");
    }

    std::string output;
    std::string first_error;
    if (!run_command(
            command,
            output,
            first_error)) {
        /*
         * Older Flatpak versions do not expose download-size in remote-ls.
         * Retry with the common three-column surface before treating the
         * installation as unavailable.
         */
        command.back() =
            "--columns=application,ref,version";
        if (!run_command(
                command,
                output,
                error)) {
            if (error.empty()) {
                error = first_error;
            }
            return false;
        }
    }

    parse_flatpak_output(
        output,
        user ? "User" : "System",
        updates,
        seen);
    error.clear();
    return true;
}

bool run_optional_flatpak(
    const std::vector<std::string> &arguments,
    std::string &error)
{
    std::string output;
    if (run_command(arguments, output, error)) {
        return true;
    }

    /*
     * A user or system Flatpak installation may not exist on every host.
     * Treat the standard "No such installation/no installed refs" class as a
     * no-op only when Flatpak itself is available.
     */
    if (error.find("No such installation") != std::string::npos ||
        error.find("Nothing unused to uninstall") != std::string::npos ||
        error.find("Nothing to do") != std::string::npos) {
        error.clear();
        return true;
    }
    return false;
}

std::string current_gtk_theme()
{
    if (!program_available("gsettings")) {
        return {};
    }
    std::string output;
    std::string error;
    if (!run_command(
            {"gsettings", "get",
             "org.cinnamon.desktop.interface",
             "gtk-theme"},
            output,
            error)) {
        return {};
    }

    std::string theme = trim(output);
    if (theme.size() >= 2U &&
        ((theme.front() == '\'' &&
          theme.back() == '\'') ||
         (theme.front() == '"' &&
          theme.back() == '"'))) {
        theme =
            theme.substr(1U, theme.size() - 2U);
    }
    return theme;
}

bool valid_flatpak_component(
    const std::string_view value)
{
    return !value.empty() &&
           std::all_of(
               value.begin(),
               value.end(),
               [](const unsigned char ch) {
                   return std::isalnum(ch) != 0 ||
                          ch == '.' || ch == '-' ||
                          ch == '_';
               });
}

bool install_matching_theme(
    const bool user,
    std::string &error)
{
    const std::string theme = current_gtk_theme();
    if (!valid_flatpak_component(theme)) {
        return true;
    }

    const std::string ref =
        "org.gtk.Gtk3theme." + theme;
    std::vector<std::string> remotes_command{
        "flatpak",
        "remotes",
        "--columns=name"
    };
    if (user) {
        remotes_command.insert(
            remotes_command.begin() + 2,
            "--user");
    } else {
        remotes_command.insert(
            remotes_command.begin() + 2,
            "--system");
    }

    std::string remotes;
    std::string ignored_error;
    if (!run_command(
            remotes_command,
            remotes,
            ignored_error)) {
        error =
            "Unable to inspect Flatpak remotes while matching the host theme: " +
            ignored_error;
        return false;
    }

    for (const std::string_view line :
         split(remotes, '\n')) {
        const std::string remote = trim(line);
        if (!valid_flatpak_component(remote)) {
            continue;
        }

        std::vector<std::string> info{
            "flatpak", "remote-info"
        };
        if (user) {
            info.push_back("--user");
        } else {
            info.push_back("--system");
        }
        info.push_back(remote);
        info.push_back(ref);

        std::string output;
        if (!run_command(
                info,
                output,
                ignored_error)) {
            continue;
        }

        std::vector<std::string> install{
            "flatpak", "install",
            "-y", "--noninteractive"
        };
        if (user) {
            install.push_back("--user");
        } else {
            install.push_back("--system");
        }
        install.push_back(remote);
        install.push_back(ref);
        if (!run_command(
                install,
                output,
                ignored_error)) {
            error =
                "Unable to install matching Flatpak theme " +
                ref + ": " + ignored_error;
            return false;
        }
        return true;
    }
    return true;
}

ExternalUpdateKind cinnamon_kind(
    const std::string_view type)
{
    if (type == "desklet") {
        return ExternalUpdateKind::cinnamon_desklet;
    }
    if (type == "extension") {
        return ExternalUpdateKind::cinnamon_extension;
    }
    if (type == "theme") {
        return ExternalUpdateKind::cinnamon_theme;
    }
    if (type == "action") {
        return ExternalUpdateKind::nemo_action;
    }
    return ExternalUpdateKind::cinnamon_applet;
}

} // namespace

std::string_view external_update_kind_name(
    const ExternalUpdateKind kind) noexcept
{
    switch (kind) {
    case ExternalUpdateKind::flatpak_application:
        return "Flatpak application";
    case ExternalUpdateKind::flatpak_runtime:
        return "Flatpak runtime";
    case ExternalUpdateKind::cinnamon_applet:
        return "Cinnamon applet";
    case ExternalUpdateKind::cinnamon_desklet:
        return "Cinnamon desklet";
    case ExternalUpdateKind::cinnamon_extension:
        return "Cinnamon extension";
    case ExternalUpdateKind::cinnamon_theme:
        return "Cinnamon theme";
    case ExternalUpdateKind::nemo_action:
        return "Nemo action";
    }
    return "External update";
}

bool discover_flatpak_updates(
    std::vector<ExternalUpdate> &updates,
    std::string &error)
{
    updates.clear();
    error.clear();
    if (!program_available("flatpak")) {
        return true;
    }

    std::set<std::string> seen;
    std::string system_error;
    std::string user_error;
    const bool system_ok =
        discover_flatpak_installation(
            false,
            updates,
            seen,
            system_error);
    const bool user_ok =
        discover_flatpak_installation(
            true,
            updates,
            seen,
            user_error);

    if (!system_ok || !user_ok) {
        error = system_ok
            ? "User Flatpak installation: " + user_error
            : "System Flatpak installation: " + system_error;
        if (!system_ok && !user_ok) {
            error += "; User Flatpak installation: " + user_error;
        }
        updates.clear();
        return false;
    }
    return true;
}

bool discover_cinnamon_updates(
    std::vector<ExternalUpdate> &updates,
    std::string &error)
{
    return discover_native_cinnamon_updates(updates, error);
}

bool apply_flatpak_updates(
    const bool remove_unused,
    const bool match_host_theme,
    std::string &error,
    ExternalProgressCallback progress)
{
    error.clear();
    if (!program_available("flatpak")) {
        return true;
    }

    if (remove_unused) {
        for (const bool user : {false, true}) {
            if (progress) progress(user
                ? "Removing unused user Flatpak runtimes"
                : "Removing unused system Flatpak runtimes");
            std::vector<std::string> command{
                "flatpak", "uninstall",
                "--unused", "-y",
                "--noninteractive"
            };
            command.push_back(
                user ? "--user" : "--system");
            if (!run_optional_flatpak(
                    command, error)) {
                return false;
            }
        }
    }

    if (match_host_theme) {
        if (progress) progress("Checking desktop theme runtimes");
        if (!install_matching_theme(false, error) ||
            !install_matching_theme(true, error)) {
            return false;
        }
    }

    for (const bool user : {false, true}) {
        if (progress) progress(user
            ? "Downloading and installing user Flatpak updates"
            : "Downloading and installing system Flatpak updates");
        std::vector<std::string> command{
            "flatpak", "update",
            "-y", "--noninteractive"
        };
        command.push_back(
            user ? "--user" : "--system");
        if (!run_optional_flatpak(
                command, error)) {
            return false;
        }
    }
    return true;
}

bool apply_cinnamon_updates(
    std::string &error,
    ExternalProgressCallback progress)
{
    std::vector<ExternalUpdate> updates;
    if (!discover_native_cinnamon_updates(updates, error)) {
        return false;
    }
    return apply_native_cinnamon_updates_selected(
        updates, error, std::move(progress));
}

bool apply_cinnamon_updates_selected(
    const std::vector<ExternalUpdate> &selected,
    std::string &error,
    ExternalProgressCallback progress)
{
    return apply_native_cinnamon_updates_selected(
        selected, error, std::move(progress));
}

bool apply_flatpak_updates_selected(
    const std::vector<ExternalUpdate> &selected,
    std::string &error,
    ExternalProgressCallback progress)
{
    error.clear();
    if (selected.empty()) return true;
    if (!program_available("flatpak")) {
        error="Flatpak is unavailable; selected updates were not installed.";
        return false;
    }
    for (const ExternalUpdate &update : selected) {
        if (update.backend != "Flatpak" ||
            (update.ref.rfind("app/",0U) != 0U &&
             update.ref.rfind("runtime/",0U) != 0U) ||
            update.ref.find_first_of(" \t\r\n") != std::string::npos) {
            error="Invalid Flatpak update selection.";
            return false;
        }
    }

    std::size_t index = 0U;
    for (const ExternalUpdate &update : selected) {
        ++index;
        if (progress) {
            std::string status =
                "Flatpak " + std::to_string(index) + "/" +
                std::to_string(selected.size()) + " • " +
                (update.name.empty() ? update.id : update.name);
            if (update.download_bytes > 0U) {
                status += " • " +
                    std::to_string(update.download_bytes) +
                    " bytes planned";
            }
            progress(status);
        }

        std::vector<std::string> command{
            "flatpak","update","-y","--noninteractive",
            update.user_installation ? "--user" : "--system",
            "--", update.ref};
        std::string output;
        if (!run_command_streaming(
                command,
                output,
                error,
                [&](std::string_view detail) {
                    if (progress) {
                        progress(
                            "Flatpak " + std::to_string(index) + "/" +
                            std::to_string(selected.size()) +
                            " • " + std::string(detail));
                    }
                })) {
            return false;
        }
        if (progress) {
            progress(
                "Flatpak " + std::to_string(index) + "/" +
                std::to_string(selected.size()) +
                " • completed " + update.ref);
        }
    }
    return true;
}

} // namespace infiltrator::software
