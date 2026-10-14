#!/usr/bin/env python3
from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one exact replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


def sub_once(path: str, pattern: str, replacement: str) -> None:
    target = Path(path)
    text = target.read_text()
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.DOTALL)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex replacement, found {count}")
    target.write_text(updated)


# Installed-state truth is engine-only. Remove the stale flag left behind by the
# old fallback design instead of restoring a second source of authority.
replace_once(
    "src/app/system_data.hpp",
    "    std::string update_warning;\n    bool from_engine{false};\n",
    "    std::string update_warning;\n",
)
replace_once(
    "src/app/system_data.cpp",
    "        infiltrator::software::read_installed_packages(\n            result.error,\n            &result.from_engine);",
    "        infiltrator::software::read_installed_packages(result.error);",
)
replace_once(
    "src/app/system_view.cpp",
    "    std::string update_warning;\n    bool from_engine{false};\n",
    "    std::string update_warning;\n",
)
replace_once(
    "src/app/system_view.cpp",
    "        result->update_warning = std::move(loaded.update_warning);\n        result->from_engine = loaded.from_engine;",
    "        result->update_warning = std::move(loaded.update_warning);",
)
replace_once(
    "src/app/system_view.cpp",
    "    const std::string warning = result->update_warning;\n    const bool from_engine = result->from_engine;\n    delete result;",
    "    const std::string warning = result->update_warning;\n    delete result;",
)
replace_once(
    "src/app/system_view.cpp",
    "            message\n                << state->system.system_records.size()\n                << \" kernel, driver and core system components read \"\n                << (from_engine\n                        ? \"from the shared native package engine.\"\n                        : \"from direct Debian package state.\");",
    "            message\n                << state->system.system_records.size()\n                << \" kernel, driver and core system components read \"\n                << \"from the shared native package engine.\";",
)

# app/process_support is the sole low-level process-construction boundary for
# the main GUI. It owns argv marshalling, GError lifetime and synchronous pipe
# communication; callers retain product-specific exit-code semantics.
replace_once(
    "src/app/process_support.hpp",
    "[[nodiscard]] GSubprocess *start_subprocess(\n    const std::vector<std::string> &arguments,\n    GSubprocessFlags flags,\n    std::string &error);\n",
    "[[nodiscard]] GSubprocess *start_subprocess(\n    const std::vector<std::string> &arguments,\n    GSubprocessFlags flags,\n    std::string &error);\n\nstruct SubprocessResult {\n    std::string standard_output;\n    std::string standard_error;\n    bool successful{false};\n};\n\n/*\n * Run one subprocess to completion while keeping construction, pipe and GError\n * ownership inside the GUI process boundary. The return value describes the\n * ability to start and communicate with the process; child exit success is\n * deliberately separate so each caller retains its product-specific policy.\n */\n[[nodiscard]] bool run_subprocess_sync(\n    const std::vector<std::string> &arguments,\n    SubprocessResult &result,\n    std::string &error);\n",
)
replace_once(
    "src/app/process_support.cpp",
    "    if (process == nullptr &&\n        gerror != nullptr && gerror->message != nullptr) {\n        error = gerror->message;\n    }\n    g_clear_error(&gerror);\n    return process;\n}\n\n} // namespace infiltrator::software::app\n",
    "    if (process == nullptr) {\n        error =\n            gerror != nullptr && gerror->message != nullptr\n                ? std::string(gerror->message)\n                : \"Unable to start subprocess.\";\n    }\n    g_clear_error(&gerror);\n    return process;\n}\n\nbool run_subprocess_sync(\n    const std::vector<std::string> &arguments,\n    SubprocessResult &result,\n    std::string &error)\n{\n    result = {};\n    GSubprocess *process = start_subprocess(\n        arguments,\n        static_cast<GSubprocessFlags>(\n            G_SUBPROCESS_FLAGS_STDOUT_PIPE |\n            G_SUBPROCESS_FLAGS_STDERR_PIPE),\n        error);\n    if (process == nullptr) {\n        return false;\n    }\n\n    gchar *standard_output = nullptr;\n    gchar *standard_error = nullptr;\n    GError *gerror = nullptr;\n    const gboolean communicated = g_subprocess_communicate_utf8(\n        process, nullptr, nullptr, &standard_output, &standard_error, &gerror);\n    if (!communicated) {\n        error =\n            gerror != nullptr && gerror->message != nullptr\n                ? std::string(gerror->message)\n                : \"Unable to communicate with subprocess.\";\n        g_free(standard_output);\n        g_free(standard_error);\n        g_clear_error(&gerror);\n        g_object_unref(process);\n        return false;\n    }\n\n    if (standard_output != nullptr) {\n        result.standard_output = standard_output;\n    }\n    if (standard_error != nullptr) {\n        result.standard_error = standard_error;\n    }\n    result.successful = g_subprocess_get_successful(process) != FALSE;\n\n    g_free(standard_output);\n    g_free(standard_error);\n    g_clear_error(&gerror);\n    g_object_unref(process);\n    error.clear();\n    return true;\n}\n\n} // namespace infiltrator::software::app\n",
)

replace_once(
    "src/app/main.cpp",
    '#include "app/page_registry.hpp"\n',
    '#include "app/page_registry.hpp"\n#include "app/process_support.hpp"\n',
)
sub_once(
    "src/app/main.cpp",
    r"void shell_ensure_update_indicator\(\)\n\{.*?\n\}\n\ngboolean shell_window_key_pressed",
    '''void shell_ensure_update_indicator()\n{\n    std::string error;\n    GSubprocess *process = start_subprocess(\n        {"infiltrator-software-tray"},\n        G_SUBPROCESS_FLAGS_NONE,\n        error);\n    if (process == nullptr) {\n        if (!error.empty()) {\n            g_warning(\n                "Unable to start software update indicator: %s",\n                error.c_str());\n        }\n        return;\n    }\n\n    /*\n     * The tray is a separately owned long-lived process. Dropping the client\n     * handle does not terminate it; the tray's own single-instance contract\n     * resolves duplicate launch attempts.\n     */\n    g_object_unref(process);\n}\n\ngboolean shell_window_key_pressed''',
)

replace_once(
    "src/app/repair_diagnostics.cpp",
    '#include "app/repair_diagnostics.hpp"\n\n',
    '#include "app/repair_diagnostics.hpp"\n\n#include "app/process_support.hpp"\n',
)
sub_once(
    "src/app/repair_diagnostics.cpp",
    r"bool run_dpkg_audit\(std::string &diagnostic, std::string &error\)\n\{.*?\n\}\n\nbool pending_dpkg_update_fragments",
    '''bool run_dpkg_audit(std::string &diagnostic, std::string &error)\n{\n    diagnostic.clear();\n    error.clear();\n\n    SubprocessResult process;\n    if (!run_subprocess_sync({"dpkg", "--audit"}, process, error)) {\n        if (error.empty()) {\n            error = "Unable to run dpkg package-state diagnostics.";\n        }\n        return false;\n    }\n    if (!process.successful) {\n        error =\n            !process.standard_error.empty()\n                ? single_line(process.standard_error)\n                : "dpkg package-state diagnostics failed.";\n        return false;\n    }\n\n    diagnostic = single_line(process.standard_output);\n    return true;\n}\n\nbool pending_dpkg_update_fragments''',
)

sub_once(
    "src/app/system_view.cpp",
    r"    gchar \*stdout_text = nullptr;.*?    \} else if \(stdout_text != nullptr\) \{\n        std::istringstream input\(stdout_text\);",
    '''    SubprocessResult process;\n    std::string process_error;\n    const bool communicated = run_subprocess_sync(\n        {"/usr/bin/infiltrator-software-release-upgrade", "plan"},\n        process,\n        process_error);\n\n    if (!communicated) {\n        result->error =\n            process_error.empty()\n                ? "Unable to calculate the release-upgrade plan."\n                : process_error;\n    } else if (!process.successful) {\n        result->error =\n            !process.standard_error.empty()\n                ? single_line(process.standard_error)\n                : "Unable to calculate the release-upgrade plan.";\n    } else {\n        std::istringstream input(process.standard_output);''',
)
replace_once(
    "src/app/system_view.cpp",
    "\n    g_free(stdout_text);\n    g_free(stderr_text);\n    g_clear_error(&gerror);\n\n    g_task_return_pointer(",
    "\n    g_task_return_pointer(",
)

# The core history module already owns this path convention.
sub_once(
    "src/app/kernel_manager.cpp",
    r"std::filesystem::path history_path\(\)\n\{.*?\n\}\n\nvoid record_history\(\n    const TransactionPlan &plan,\n    const bool success,\n    const std::string_view message\)\n\{.*?\n\}\n\nvoid clear_list",
    '''void record_history(\n    const TransactionPlan &plan,\n    const bool success,\n    const std::string_view message)\n{\n    const std::string path = user_transaction_history_path();\n    if (path.empty() || plan.items.empty()) {\n        return;\n    }\n\n    TransactionHistoryStore store(path);\n    std::string error;\n    if (!store.append(plan, success, message, error)) {\n        g_warning(\n            "Unable to record kernel transaction history: %s",\n            error.c_str());\n    }\n}\n\nvoid clear_list''',
)
kernel_path = Path("src/app/kernel_manager.cpp")
kernel_text = kernel_path.read_text()
if "std::filesystem" not in kernel_text:
    kernel_path.write_text(kernel_text.replace("#include <filesystem>\n", "", 1))

# Common's atomic POSIX writer is errno-style: zero is success.
replace_once(
    "src/catalogue/repository_catalogue.cpp",
    "    if (!infiltratr_atomic_file_write_bytes(\n            path.c_str(),\n            INFILTRATR_ATOMIC_FILE_PRIVATE,\n            content.data(),\n            content.size())) {\n        error = \"Unable to write the catalogue cache atomically.\";\n        return false;\n    }\n    return true;",
    "    const int status =\n        infiltratr_atomic_file_write_bytes(\n            path.c_str(),\n            INFILTRATR_ATOMIC_FILE_PRIVATE,\n            content.data(),\n            content.size());\n    if (status != 0) {\n        error =\n            \"Unable to write the catalogue cache atomically (status \" +\n            std::to_string(status) + \").\";\n        return false;\n    }\n    return true;",
)

# Cinnamon Spice archives are executable user content and do not carry an
# independent cryptographic digest, so redirect policy must not downgrade HTTPS.
replace_once(
    "src/external/cinnamon_spices.cpp",
    '''bool curl_ready()\n{\n    static const CURLcode status = curl_global_init(CURL_GLOBAL_DEFAULT);\n    return status == CURLE_OK;\n}\n''',
    '''bool curl_ready()\n{\n    static const CURLcode status = curl_global_init(CURL_GLOBAL_DEFAULT);\n    return status == CURLE_OK;\n}\n\nbool configure_https_only(CURL *curl)\n{\n    if (curl == nullptr) {\n        return false;\n    }\n#if LIBCURL_VERSION_NUM >= 0x075500\n    const CURLcode protocols =\n        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");\n    const CURLcode redirects =\n        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");\n#else\n    const CURLcode protocols =\n        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);\n    const CURLcode redirects =\n        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);\n#endif\n    const CURLcode redirect_limit =\n        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);\n    return protocols == CURLE_OK &&\n           redirects == CURLE_OK &&\n           redirect_limit == CURLE_OK;\n}\n''',
)
replace_once(
    "src/external/cinnamon_spices.cpp",
    '''    if (curl == nullptr) {\n        error = "Unable to create the HTTPS request.";\n        return false;\n    }\n\n    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());''',
    '''    if (curl == nullptr) {\n        error = "Unable to create the HTTPS request.";\n        return false;\n    }\n    if (!configure_https_only(curl)) {\n        curl_easy_cleanup(curl);\n        error = "Unable to enforce HTTPS-only Cinnamon Spice transport.";\n        return false;\n    }\n\n    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());''',
)
replace_once(
    "src/external/cinnamon_spices.cpp",
    '''    if (curl == nullptr) {\n        std::fclose(file);\n        error = "Unable to create the Cinnamon Spice HTTPS request.";\n        return false;\n    }\n\n    const std::string url =''',
    '''    if (curl == nullptr) {\n        std::fclose(file);\n        error = "Unable to create the Cinnamon Spice HTTPS request.";\n        return false;\n    }\n    if (!configure_https_only(curl)) {\n        curl_easy_cleanup(curl);\n        std::fclose(file);\n        error = "Unable to enforce HTTPS-only Cinnamon Spice transport.";\n        return false;\n    }\n\n    const std::string url =''',
)

# Maintained documentation should point at authorities instead of duplicating a
# partial protocol inventory or promising removal of the constrained executor.
replace_once(
    "docs/STATE.md",
    "The initial D-Bus service is now implemented. It exposes `GetStatus`, `ListInstalled`, `ListUpdates`, `PlanTransaction` and `ReloadState` on `net.ssmith.infiltrator.software.Engine`, with `StateChanged` and `HealthChanged` signals. Package-state database reads are read-only and do not require clients to own or mutate the database. The service caches update state per generation so multiple clients consume the same calculation.",
    "The D-Bus service is implemented from the installed interface XML, which is the protocol-schema authority. It exposes status and health, installed/update/kernel snapshots, exact and mixed transaction planning, installed/full refresh and reload controls, and controlled shutdown, with `StateChanged` and `HealthChanged` signals. Package-state database reads are read-only and do not require clients to own or mutate the database. The service caches update state per generation so multiple clients consume the same calculation.",
)
replace_once(
    "debian/control",
    " The native Debian compatibility engine is replacing APT command\n dependencies while retaining Debian repositories and .deb compatibility.\n",
    " The native Debian engine owns package selection, dependency resolution and\n reviewed plans; APT remains a constrained privileged executor for verified\n .deb transactions.\n",
)

# Protect architectural semantics rather than local expression shapes.
replace_once(
    "tests/source_contracts.py",
    'system_catalogue = text("src/catalogue/system_catalogue.cpp")\n',
    'system_catalogue = text("src/catalogue/system_catalogue.cpp")\nrepository_catalogue = text("src/catalogue/repository_catalogue.cpp")\nkernel_manager = text("src/app/kernel_manager.cpp")\n',
)
replace_once(
    "tests/source_contracts.py",
    'assert "from_engine" not in installed_inventory\nassert "from_engine" not in installed_controller\n',
    'assert "from_engine" not in installed_inventory\nassert "from_engine" not in installed_controller\nassert "from_engine" not in app\n',
)
replace_once(
    "tests/source_contracts.py",
    '''# GUI subprocess ownership has exactly one low-level construction boundary.\nassert "g_subprocess_newv(" in process_support\nfor path in Path("src/app").glob("*.cpp"):\n    if path.name != "process_support.cpp":\n        assert "g_subprocess_newv(" not in path.read_text()\n''',
    '''# GUI subprocess ownership has exactly one low-level construction boundary.\n# Cover GSubprocess and legacy g_spawn entry points so choosing another GLib\n# constructor cannot silently bypass the ownership contract.\nassert "g_subprocess_newv(" in process_support\nfor path in Path("src/app").glob("*.cpp"):\n    if path.name == "process_support.cpp":\n        continue\n    source = path.read_text()\n    for low_level_process_api in (\n        "g_subprocess_new(",\n        "g_subprocess_newv(",\n        "g_subprocess_launcher_new(",\n        "g_spawn_",\n    ):\n        assert low_level_process_api not in source, (\n            f"{path} bypasses app/process_support with {low_level_process_api}"\n        )\n''',
)
replace_once(
    "tests/source_contracts.py",
    '''# Signed repository cache reuse remains tied to verified metadata.\nfor token in ("cached_release_current", "verify_payload", "read_cached"):\n    assert token in repository\n''',
    '''# Signed repository cache reuse remains tied to verified metadata.\nfor token in ("cached_release_current", "verify_payload", "read_cached"):\n    assert token in repository\n\n# Common's atomic POSIX writer is errno-style: zero is success. Reversing this\n# contract makes successful cache publication look like failure and actual\n# write failures look successful.\natomic_write = re.search(\n    r"const int (?P<status>[A-Za-z_]\\w*)\\s*=\\s*"\n    r"infiltratr_atomic_file_write_bytes\\(.*?\\);\\s*"\n    r"if \\((?P=status) != 0\\)",\n    repository_catalogue,\n    re.DOTALL,\n)\nassert atomic_write is not None\n''',
)
replace_once(
    "tests/source_contracts.py",
    '''assert '"cinnamon-spice-updater"' not in external_updates\nassert 'std::string("flatpak:")' in system_catalogue\nassert "installed_flatpaks" in system_catalogue\n''',
    '''assert '"cinnamon-spice-updater"' not in external_updates\nassert 'std::string("flatpak:")' in system_catalogue\nassert "installed_flatpaks" in system_catalogue\nassert "CURLOPT_PROTOCOLS_STR" in cinnamon_native\nassert "CURLOPT_REDIR_PROTOCOLS_STR" in cinnamon_native\nassert cinnamon_native.count("configure_https_only(curl);") == cinnamon_native.count("curl_easy_perform(curl)")\n''',
)
replace_once(
    "tests/source_contracts.py",
    '''assert "system_transaction_history_path" in maintenance\nassert "system_on_battery" in session_updater\n''',
    '''assert "system_transaction_history_path" in maintenance\nassert "history.sqlite3" not in kernel_manager\nassert "user_transaction_history_path()" in kernel_manager\nassert "system_on_battery" in session_updater\n''',
)

replace_once(
    "debian/changelog",
    "  * Add focused Multi-Arch removal regressions and strengthen architecture and\n    documentation contracts around the consolidated ownership boundaries.\n",
    "  * Add focused Multi-Arch removal regressions and strengthen architecture and\n    documentation contracts around the consolidated ownership boundaries.\n  * Complete a repository-wide forensic pass: remove stale dual-source state,\n    consolidate GUI subprocess construction and user history-path ownership,\n    and correct catalogue-cache handling of Common's errno-style atomic writer.\n  * Fail closed on Cinnamon Spice HTTPS redirects, advance the exact Common pin\n    to 1.19.42, and align maintained state/package documentation with reality.\n",
)
replace_once(
    "debian/infiltrator-software.metainfo.xml",
    "        <p>Consolidates dependency, repository-source and D-Bus authorities; fixes Multi-Arch removal safety and Installed/Flatpak failure reporting; and removes a read-only dpkg architecture-query subprocess.</p>",
    "        <p>Consolidates dependency, repository-source, D-Bus, GUI-process and history-path authorities; fixes Multi-Arch removal safety, catalogue cache publication, Installed/Flatpak failure reporting and Cinnamon HTTPS redirect confinement; and advances the qualified Common dependency.</p>",
)
