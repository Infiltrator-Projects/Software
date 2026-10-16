#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Durable architecture, authorization and release contracts for Software.

This test intentionally checks boundaries and externally meaningful invariants.
Compiled unit/integration tests own algorithmic behaviour; this file must not
make ordinary refactors fail merely because a local expression was rewritten.
"""

from pathlib import Path
import base64
import re
import xml.etree.ElementTree as ET


def text(path: str) -> str:
    return Path(path).read_text()


def includes(source: str, header: str) -> bool:
    return re.search(
        rf'^\s*#\s*include\s+"{re.escape(header)}"\s*$', source, re.MULTILINE
    ) is not None


def target_links(cmake: str, target: str) -> str:
    match = re.search(
        rf'target_link_libraries\s*\(\s*{re.escape(target)}(?=\s)(.*?)\)',
        cmake,
        re.DOTALL,
    )
    assert match is not None, f"missing target_link_libraries for {target}"
    return match.group(1)


app_files = sorted(
    list(Path("src/app").glob("*.cpp")) +
    list(Path("src/app").glob("*.hpp"))
)
app = "\n".join(path.read_text() for path in app_files)
cmake = text("CMakeLists.txt")
model = text("src/core/model.hpp")
main_cpp = text("src/app/main.cpp")
app_coordinator = text("src/app/app_coordinator.cpp")
discover_view = text("src/app/discover_view.cpp")
updates_view = text("src/app/updates_view.cpp")
process_support = text("src/app/process_support.cpp")
installed_inventory = text("src/app/installed_inventory.cpp")
installed_controller = text("src/app/installed_controller.cpp")
repository_controller_hpp = text("src/app/repository_controller.hpp")
engine_client = text("src/client/engine_client.cpp")
engine_client_hpp = text("src/client/engine_client.hpp")
engine_service = text("src/service/main.cpp")
engine_core = text("src/engine/engine_service_core.cpp")
engine_introspection_template = text("src/service/engine_introspection.hpp.in")
engine_interface = text("data/net.ssmith.software.Engine.xml")
transaction = text("src/engine/debian_transaction.cpp")
dependency = text("src/engine/debian_dependency.cpp")
candidate = text("src/engine/debian_candidate.cpp")
package_policy = text("src/engine/package_policy.cpp")
phased = text("src/engine/debian_phased_updates.cpp")
preferences = text("src/engine/debian_preferences.cpp")
reconcile = text("src/engine/debian_reconcile.cpp")
repository = text("src/engine/debian_repository.cpp")
source_configuration = text("src/engine/debian_source_configuration.cpp")
debian_control = text("src/engine/debian_control.cpp")
debian_installed_state = text("src/engine/debian_installed_state.cpp")
debian_package_index = text("src/engine/debian_package_index.cpp")
state_store = text("src/engine/package_state_store.cpp")
exact_spec = text("src/core/exact_transaction_spec.cpp")
source_inventory = text("src/sources/source_inventory.cpp")
source_mutation = text("src/sources/source_mutation.cpp")
source_format = text("src/sources/apt_source_format.cpp")
source_helper = text("src/helper/source_helper.cpp")
helper = text("src/helper/update_helper.cpp")
guard = text("src/helper/apt_plan_guard.cpp")
external_updates = text("src/external/external_updates.cpp")
cinnamon_native = text("src/external/cinnamon_spices.cpp")
system_catalogue = text("src/catalogue/system_catalogue.cpp")
repository_catalogue = text("src/catalogue/repository_catalogue.cpp")
kernel_manager = text("src/app/kernel_manager.cpp")
auto_updater = text("src/automation/main.cpp")
maintenance = text("src/automation/maintenance.cpp")
session_updater = text("src/session/main.cpp")
release_upgrader = text("src/release/main.cpp")
tray = text("src/tray/main.cpp")
cli = text("src/cli/main.cpp")
control = text("debian/control")
parity = text("docs/MINTUPDATE_PARITY.md")
workflow = text(".github/workflows/ci.yml")
version = text("VERSION").strip()
readme = text("README.md")
changelog = text("debian/changelog")

metainfo_root = ET.parse("debian/software.metainfo.xml").getroot()
metainfo_releases = metainfo_root.findall("./releases/release")
update_policy_path = Path("data/net.ssmith.software.updates.policy")
source_policy_path = Path("data/net.ssmith.software.policy")
update_policy_xml = update_policy_path.read_text()

# One release identity, everywhere users and package tooling observe it.
assert "[`VERSION`](VERSION)" in readme
assert changelog.startswith(f"software ({version}) ")
assert metainfo_releases
assert metainfo_releases[0].get("version") == version
for marker in ("Warning: truncated output", "tokens truncated", "Total output lines:"):
    assert marker not in changelog

# Verification and publication are one linear authority. The exact bundle that
# passed qualification is published from the same job and filesystem: no
# Actions-artifact relay, no second checkout, and no rebuilt release package.
assert "group: verify-${{ github.repository }}-${{ github.ref }}" in workflow
assert "cancel-in-progress: true" in workflow
assert workflow.count("dpkg-buildpackage -us -uc -b") == 1
assert workflow.count("publish-github") == 1
assert "actions/upload-artifact@" not in workflow
assert "actions/download-artifact@" not in workflow
assert "\n  publish:" not in workflow
assert "persist-credentials: false" in workflow
assert workflow.index("complete-bundle") < workflow.index("publish-github")
assert "dist/release" in workflow

# Mint Update parity is an audited capability matrix, not prose claiming that
# an installed host has already completed destructive qualification.
required_rows = [
    line for line in parity.splitlines()
    if line.startswith("| ") and line.rstrip().endswith("| Required |")
]
assert required_rows
assert all(len(line.split("|")) == 5 for line in required_rows)
assert "Removal approved after host validation" not in parity

# Dependency direction: graphical code consumes domain/client/controller APIs,
# never engine implementation headers or direct package-state readers.
assert "AptBackend" not in app
assert "add_library(software-apt STATIC" not in cmake
assert not re.search(r'^\s*#\s*include\s+"engine/', app, re.MULTILINE)
assert "EngineClient" in installed_inventory
assert "DebianInstalledState::read" not in installed_inventory
assert '"engine/debian_installed_state.hpp"' not in installed_inventory
assert "from_engine" not in installed_inventory
assert "from_engine" not in installed_controller
assert "from_engine" not in app
assert '"Debian state"' not in installed_controller
assert '"Debian + Flatpak"' not in installed_controller
assert '"engine/kernel_inventory.hpp"' not in engine_client_hpp
assert '"sources/source_inventory.hpp"' not in repository_controller_hpp
assert "struct KernelRecord" in model
assert "struct SourceRecord" in model

# The executable shell and event coordinator remain insulated from page
# implementations, allowing pages/controllers to change independently.
for header in (
    "app/discover_view.hpp",
    "app/updates_view.hpp",
    "app/updates_controller.hpp",
):
    assert not includes(main_cpp, header)
for header in (
    "app/discover_view.hpp",
    "app/updates_view.hpp",
    "app/system_view.hpp",
    "app/repair_view.hpp",
    "app/repository_view.hpp",
    "app/installed_controller.hpp",
    "app/history_controller.hpp",
):
    assert not includes(app_coordinator, header)
assert includes(app_coordinator, "app/page_registry.hpp")
assert includes(app_coordinator, "app/view_intents.hpp")
assert includes(app_coordinator, "app/window_state.hpp")
assert not includes(discover_view, "app/updates_view.hpp")
assert not includes(updates_view, "app/discover_view.hpp")
assert includes(discover_view, "app/page_support.hpp")
assert includes(updates_view, "app/page_support.hpp")
assert "destroy_installed_controller" in installed_controller

# GUI subprocess ownership has exactly one low-level construction boundary.
# Cover GSubprocess and legacy g_spawn entry points so choosing another GLib
# constructor cannot silently bypass the ownership contract.
assert "g_subprocess_newv(" in process_support
for path in Path("src/app").glob("*.cpp"):
    if path.name == "process_support.cpp":
        continue
    source = path.read_text()
    for low_level_process_api in (
        "g_subprocess_new(",
        "g_subprocess_newv(",
        "g_subprocess_launcher_new(",
        "g_spawn_",
    ):
        assert low_level_process_api not in source, (
            f"{path} bypasses app/process_support with {low_level_process_api}"
        )

# All Debian deb822 consumers share one syntax authority. Read-only inventory
# and privileged mutation remain separate link capabilities, and the engine
# uses the same parser for trust-sensitive repository configuration.
for target in (
    "software-source-format",
    "software-source-inventory",
    "software-source-mutation",
):
    assert f"add_library({target} STATIC" in cmake
assert "software-sources" not in cmake
assert "software-source-inventory" in target_links(cmake, "software")
assert "software-source-mutation" not in target_links(cmake, "software")
assert "software-source-mutation" in target_links(cmake, "software-helper")
assert re.search(
    r"target_link_libraries\s*\(\s*software-engine-core\s+PRIVATE\s+software-source-format\s*\)",
    cmake,
)
assert includes(source_inventory, "sources/apt_source_format.hpp")
assert includes(source_mutation, "sources/apt_source_format.hpp")
assert includes(source_configuration, "sources/apt_source_format.hpp")
assert "parse_apt_deb822_stanzas" in source_format
assert "parse_apt_deb822_stanzas" in source_inventory
assert "parse_apt_deb822_stanzas" in source_mutation
assert "parse_apt_deb822_stanzas" in source_configuration
assert "next_stanza_boundary" not in source_inventory
assert "next_stanza_boundary" not in source_mutation
assert "parse_fields" not in source_configuration

# Debian package/control paragraphs likewise have one canonical parser for
# installed state and repository package indexes.
assert "src/engine/debian_control.cpp" in cmake
assert "parse_debian_control_records" in debian_control
assert "parse_debian_control_records" in debian_installed_state
assert "parse_debian_control_records" in debian_package_index

# Exact reviewed package transactions remain capabilities, not hints. Compiled
# tests exercise encoding/solver/guard behaviour; these checks protect the
# architectural pieces that must remain in the production path.
for token in ('"x3|"', "source_fingerprint"):
    assert token in exact_spec
for token in (
    "verify_reviewed_repository_state",
    "verify_downloaded_artifacts",
    "validate_apt_simulation",
    "approved_spec_already_satisfied",
    '"--download-only"',
    '"--no-download"',
    '"--no-remove"',
):
    assert token in helper
for token in ("parse_remove_line", "introduced an unapproved change"):
    assert token in guard
for token in (
    "plan_removal",
    "Refusing to remove Essential/Protected package",
    "protected_package",
    "held",
):
    assert token in transaction
assert "DebianDependencyResolver::resolve" in transaction
for retired_helper in (
    "retained_package_matches",
    "retained_package_provides",
    "retained_group_satisfied",
):
    assert retired_helper not in transaction
for token in (
    "install_recommends",
    "planned_removals",
    "available_candidates",
    "conflict_resolution_candidates",
    "installed_identity_held",
    "candidate_architecture_matches",
    "installed_architecture_matches",
    "remove_installed",
):
    assert token in dependency or token in transaction

# Failed repository refreshes are explicit live service state. A retained
# generation is displayable but cannot silently become plan-authoritative until
# a full metadata refresh succeeds.
assert "record_refresh_failure(error)" in engine_service
assert "record_refresh_success()" in engine_service
assert "record_refresh_failure(refresh_error)" in engine_core
assert "refresh_error_.empty()" in engine_core

# Native read-only reconciliation reads authoritative dpkg state directly. The
# architectural contract is the absence of a process-execution capability in
# this path, not whether comments name a retired dpkg command while explaining
# why direct state is used.
assert '"/var/lib/dpkg/arch"' in reconcile
for process_api in (
    "g_spawn_",
    "g_subprocess_",
    "std::system(",
    "popen(",
    "fork(",
    "posix_spawn",
    "execv",
):
    assert process_api not in reconcile

# Candidate selection remains policy-driven and persisted with the provenance
# needed to explain why a candidate was selected.
assert "/etc/apt/preferences.d" in preferences
assert "DebianPolicyStack" in package_policy
assert "phased_updates" in reconcile
assert "host_preferences" in reconcile
assert "phased-update-percentage" in debian_package_index
assert "std::minstd_rand" in phased
assert "always-include-phased-updates" in phased.lower()
assert "never-include-phased-updates" in phased.lower()
assert "candidate.pin_priority" in candidate
for column in (
    "pin_priority INTEGER NOT NULL",
    "policy_provider TEXT NOT NULL",
    "release_origin TEXT NOT NULL",
):
    assert column in state_store

# Signed repository cache reuse remains tied to verified metadata.
for token in ("cached_release_current", "verify_payload", "read_cached"):
    assert token in repository

# Common's atomic POSIX writer is errno-style: zero is success. Reversing this
# contract makes successful cache publication look like failure and actual
# write failures look successful.
atomic_write = re.search(
    r"const int (?P<status>[A-Za-z_]\w*)\s*=\s*"
    r"infiltratr_atomic_file_write_bytes\(.*?\);\s*"
    r"if \((?P=status) != 0\)",
    repository_catalogue,
    re.DOTALL,
)
assert atomic_write is not None

# External ecosystems stay explicit. Flatpak scope and exact Cinnamon update
# ownership are behavioural-test concerns, while these symbols guard against a
# silent fallback to removed external updater processes.
for token in ("discover_flatpak_updates", "apply_flatpak_updates_selected"):
    assert token in external_updates
for token in (
    "discover_native_cinnamon_updates",
    "apply_native_cinnamon_updates_selected",
    "extract_zip_safely",
    "RENAME_EXCHANGE",
):
    assert token in cinnamon_native
assert '"cinnamon-spice-updater"' not in external_updates
assert 'std::string("flatpak:")' in system_catalogue
assert "installed_flatpaks" in system_catalogue
assert "CURLOPT_PROTOCOLS_STR" in cinnamon_native
assert "CURLOPT_REDIR_PROTOCOLS_STR" in cinnamon_native
assert cinnamon_native.count("if (!configure_https_only(curl))") == cinnamon_native.count("curl_easy_perform(curl)")

# Release upgrade/recovery and automatic maintenance retain explicit durable
# state and exact execution boundaries.
for token in (
    "kReleaseJournal",
    '"packages-applying"',
    '"packages-applied"',
    "recover_pending_release",
    "rollback_target_sources",
):
    assert token in release_upgrader
assert '"--purge-removals"' in helper
assert '"--purge-removals"' in maintenance
assert '"apt-get", "-s", "autoremove", "--purge"' in maintenance
assert '"LC_ALL", "C"' in maintenance
assert '"/usr/libexec/software-update-helper"' in maintenance

# Client/service compatibility and caller identity checks fail closed. The
# installed interface XML is also the sole D-Bus schema authority: the service
# embeds a generated header rather than carrying another hand-written XML copy.
assert "engine_version_is_compatible_with_client" in engine_client
assert "ensure_engine_identity" in engine_client
assert "GetConnectionUnixUser" in engine_client
assert "GetConnectionUnixProcessID" in engine_client
assert '"PlanMixedTransaction"' in engine_service
assert 'name="PlanMixedTransaction"' in engine_interface
assert '<method name="Quit"/>' in engine_interface
assert "SOFTWARE_ENGINE_INTERFACE_XML" in cmake
assert "engine_introspection.hpp.in" in cmake
assert "@SOFTWARE_ENGINE_INTERFACE_XML@" in engine_introspection_template
assert "kEngineIntrospectionXml" in engine_service
assert 'R"XML(' not in engine_service
api_service = re.search(r"kApiVersion\s*=\s*(\d+)U", engine_service)
api_client = re.search(r"kRequiredApiVersion\s*=\s*(\d+)U", engine_client)
assert api_service and api_client
assert api_service.group(1) == api_client.group(1)

# Root/user maintenance state converges without duplicating the system
# preferences path literal across consumers.
for source in (auto_updater, maintenance, cli, helper):
    assert "kSystemSoftwarePreferencesPath" in source
assert "system_transaction_history_path" in auto_updater
assert "system_transaction_history_path" in maintenance
assert "history.sqlite3" not in kernel_manager
assert "user_transaction_history_path()" in kernel_manager
assert "system_on_battery" in session_updater
assert "system_on_battery" in auto_updater
assert "refresh_installed" in tray
product_sources = "\n".join(
    path.read_text() for path in Path("src").rglob("*.[ch]pp")
    if "src/infiltratr-common/" not in path.as_posix()
)
assert product_sources.count(
    '"/etc/software/automatic-updates.conf"') == 1

# Privileged source mutation remains narrow and always administrator-authorized.
assert "set_apt_list_entry_enabled" in source_mutation
assert "set_apt_deb822_entry_enabled" in source_mutation
assert '"set-apt-source-enabled"' in source_helper
assert "safe_existing_apt_source" in source_helper
for policy_path in (update_policy_path, source_policy_path):
    defaults = ET.parse(policy_path).getroot().find(".//defaults")
    assert defaults is not None
    assert defaults.findtext("allow_any") == "auth_admin"
    assert defaults.findtext("allow_inactive") == "auth_admin"
    assert defaults.findtext("allow_active") == "auth_admin"
assert "Authentication is required to install, update or remove system software." in update_policy_xml

# Product capability surfaces remain wired without asserting GTK widget layout.
for token in (
    "InstalledController",
    "RepositoryController",
    "create_installed_page",
    "make_system_page",
    "make_repair_page",
    "create_history_controller_page",
    "discover_install_clicked",
    "create_snapshot_before_update",
):
    assert token in app
for option in (
    '"--security-only"',
    '"--kernel-only"',
    '"--dry-run"',
    '"--refresh-cache"',
    '"--force-confold"',
    '"--force-confnew"',
    '"--install-recommends"',
):
    assert option in cli
assert '"ignore"' in cli
assert "load_release_info" in release_upgrader
assert "build_plan" in release_upgrader

# Software is an independent updater. Package metadata must never claim or
# force replacement of Linux Mint's Update Manager.
assert "Provides: mintupdate" not in control
assert "Conflicts: mintupdate" not in control
assert "Replaces: mintupdate" not in control

# Authored raster identity is an asset contract, independent of GTK source
# arrangement.
hero_parts = [
    Path(f"src/app/discover_hero_part{index:02d}.inc")
    for index in range(1, 7)
]
assert all(part.is_file() for part in hero_parts)
encoded_hero = "".join(part.read_text().strip().strip('"') for part in hero_parts)
decoded_hero = base64.b64decode(encoded_hero, validate=True)
assert decoded_hero[:2].hex() == "ffd8"
assert decoded_hero[-2:].hex() == "ffd9"
assert len(decoded_hero) > 15000

logo_part = Path("src/app/titlebar_logo.inc")
assert logo_part.is_file()
decoded_logo = base64.b64decode(
    logo_part.read_text().strip().strip('"'), validate=True
)
assert decoded_logo[:8].hex() == "89504e470d0a1a0a"
assert len(decoded_logo) > 5000

# Common is pinned by gitlink/version and linked as a dependency rather than
# copied into Software's package payload.
assert "INFILTRATR_COMMON_EXPECTED_VERSION" in cmake
assert "INFILTRATR_COMMON_EXPECTED_COMMIT" in cmake
assert "InfiltratrCommon::Common" in cmake
