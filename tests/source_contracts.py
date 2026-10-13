#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Architectural, authorization and recovery contracts for Software.

These checks intentionally test durable boundaries and safety invariants rather
than exact GTK source layout. UI geometry/appearance belongs to compiled tests,
the launch smoke test and checked-in assets; implementation text is free to be
refactored without turning this file into a second source-code parser.
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


def defines_function(source: str, name: str) -> bool:
    return re.search(
        rf'\b{re.escape(name)}\s*\([^;{{}}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{{',
        source, re.DOTALL
    ) is not None


def has_member(source: str, type_name: str, member_name: str) -> bool:
    return re.search(
        rf'\b{re.escape(type_name)}\s+{re.escape(member_name)}\s*;', source
    ) is not None


app_files = sorted(
    list(Path("src/app").glob("*.cpp")) +
    list(Path("src/app").glob("*.hpp"))
)
app = "\n".join(path.read_text() for path in app_files)
cmake = text("CMakeLists.txt")
model = text("src/core/model.hpp")
transaction = text("src/engine/debian_transaction.cpp")
dependency = text("src/engine/debian_dependency.cpp")
candidate = text("src/engine/debian_candidate.cpp")
package_policy = text("src/engine/package_policy.cpp")
phased = text("src/engine/debian_phased_updates.cpp")
preferences = text("src/engine/debian_preferences.cpp")
reconcile = text("src/engine/debian_reconcile.cpp")
repository = text("src/engine/debian_repository.cpp")
debian_control = text("src/engine/debian_control.cpp")
debian_installed_state = text("src/engine/debian_installed_state.cpp")
debian_package_index = text("src/engine/debian_package_index.cpp")
state_store = text("src/engine/package_state_store.cpp")
exact_spec = text("src/core/exact_transaction_spec.cpp")
history = text("src/core/transaction_history.cpp")
installed_controller = text("src/app/installed_controller.cpp")
installed_controller_hpp = text("src/app/installed_controller.hpp")
process_support_cpp = text("src/app/process_support.cpp")
installed_inventory = text("src/app/installed_inventory.cpp")
main_cpp = text("src/app/main.cpp")
discover_view_cpp = text("src/app/discover_view.cpp")
updates_view_cpp = text("src/app/updates_view.cpp")
page_views = discover_view_cpp + "\n" + updates_view_cpp
app_coordinator = text("src/app/app_coordinator.cpp")
page_registry_cpp = text("src/app/page_registry.cpp")
page_support_cpp = text("src/app/page_support.cpp")
discover_state = text("src/app/discover_state.hpp")
updates_controller = text("src/app/updates_controller.cpp")
updates_controller_hpp = text("src/app/updates_controller.hpp")
system_view = text("src/app/system_view.cpp")
system_view_hpp = text("src/app/system_view.hpp")
repair_view = text("src/app/repair_view.cpp")
repair_view_hpp = text("src/app/repair_view.hpp")
repository_controller = text("src/app/repository_controller.cpp")
repository_controller_hpp = text("src/app/repository_controller.hpp")
window_state = text("src/app/window_state.hpp")
source_inventory_hpp = text("src/sources/source_inventory.hpp")
source_helper = text("src/helper/source_helper.cpp")
source_mutation = text("src/sources/source_mutation.cpp")
helper = text("src/helper/update_helper.cpp")
guard = text("src/helper/apt_plan_guard.cpp")
engine_client = text("src/client/engine_client.cpp")
engine_client_hpp = text("src/client/engine_client.hpp")
engine_service = text("src/service/main.cpp")
engine_interface = text("data/net.ssmith.infiltrator.software.Engine.xml")
external_updates = text("src/external/external_updates.cpp")
system_catalogue = text("src/catalogue/system_catalogue.cpp")
cinnamon_native = text("src/external/cinnamon_spices.cpp")
session_updater = text("src/session/main.cpp")
auto_updater = text("src/automation/main.cpp")
maintenance = text("src/automation/maintenance.cpp")
release_upgrader = text("src/release/main.cpp")
tray = text("src/tray/main.cpp")
cli = text("src/cli/main.cpp")
parity = text("docs/MINTUPDATE_PARITY.md")
control = text("debian/control")
version = text("VERSION").strip()
readme = text("README.md")
changelog = text("debian/changelog")
metainfo_path = Path("debian/infiltrator-software.metainfo.xml")
metainfo_root = ET.parse(metainfo_path).getroot()
metainfo_releases = metainfo_root.findall("./releases/release")

update_policy_path = Path("data/net.ssmith.infiltrator.software.updates.policy")
source_policy_path = Path("data/net.ssmith.infiltrator.software.policy")
policy = update_policy_path.read_text()

# Release metadata must describe the same source version everywhere. A release
# bump is incomplete if VERSION, README, changelog and AppStream diverge.
assert f"**Current source version:** {version}<br>" in readme
assert changelog.startswith(f"infiltrator-software ({version}) ")
assert metainfo_releases
assert metainfo_releases[0].get("version") == version

# Release metadata must never contain copied console/tool truncation markers.
assert "Warning: truncated output" not in changelog
assert "tokens truncated" not in changelog
assert "Total output lines:" not in changelog

# Replacement inventory is an audit catalogue, not proof that a host completed
# every real mutation path.
required_rows = [
    line for line in parity.splitlines()
    if line.startswith("| ") and line.rstrip().endswith("| Required |")
]
assert required_rows
assert all(len(line.split("|")) == 5 for line in required_rows)
assert "Removal approved after host validation" not in parity

# The app shell must consume native engine state. It may not resurrect the old
# APT backend or bypass the shared engine for installed inventory.
assert "AptBackend" not in app
assert "add_library(software-apt STATIC" not in cmake
assert "EngineClient engine" in installed_inventory
assert "DebianInstalledState::read" not in installed_inventory
assert '"engine/debian_installed_state.hpp"' not in installed_inventory
assert "engine.refresh_installed(result.error)" in updates_controller
assert "engine.refresh(result.error)" in updates_controller
assert '"RefreshInstalledState"' in engine_client
assert '<method name="RefreshInstalledState">' in engine_interface

# Public client/domain types must not leak engine implementation headers.
assert '"engine/kernel_inventory.hpp"' not in engine_client_hpp
assert "struct KernelRecord" in model
assert "struct SourceRecord" in model
assert '"sources/source_inventory.hpp"' not in repository_controller_hpp
assert '"core/model.hpp"' in repository_controller_hpp
assert '"core/model.hpp"' in source_inventory_hpp
# GUI code consumes core/client/controller APIs, never engine implementation
# headers. This keeps engine internals replaceable without UI rebuild coupling.
assert not re.search(r'^\s*#\s*include\s+"engine/', app, re.MULTILINE)

# Debian control syntax has one parser. Installed-state and repository-index
# readers supply record consumers rather than maintaining subtly different
# continuation/field rules.
assert "src/engine/debian_control.cpp" in cmake
assert "parse_debian_control_records" in debian_control
assert "parse_debian_control_records" in debian_installed_state
assert "parse_debian_control_records" in debian_package_index
assert "Malformed Debian control line without a field separator." not in debian_installed_state
assert "Malformed Debian control line without a field separator." not in debian_package_index

# Page state is owned by named members. Verify durable ownership/dependency
# boundaries rather than exact formatting or call spellings.
assert re.search(r"\bstruct\s+ShellState\b", window_state)
assert re.search(r"\bstruct\s+WindowState\s+final\s*:\s*ShellState\s*\{", window_state)
for type_name, member_name in (
    ("DiscoverPageState", "discover"),
    ("UpdatesController", "updates"),
    ("SystemPageState", "system"),
    ("RepairPageState", "repair"),
):
    assert has_member(window_state, type_name, member_name)
window_state_bases = window_state.split("struct WindowState final", 1)[1].split("{", 1)[0]
for page_base in ("DiscoverPageState", "UpdatesController", "SystemPageState", "RepairPageState"):
    assert page_base not in window_state_bases
assert "do not add new aliases" in window_state
assert app.count('"infiltrator-window-state"') == 1
assert not Path("src/app/legacy_page_aliases.hpp").exists()
assert not Path("src/app/legacy_page_impl.cpp").exists()
for path in (
    "src/app/discover_view.cpp",
    "src/app/updates_view.cpp",
    "src/app/page_support.cpp",
    "src/app/page_registry.cpp",
):
    assert path in cmake

# Discover and Updates share neutral support but never each other's view API.
assert not includes(discover_view_cpp, "app/updates_view.hpp")
assert not includes(updates_view_cpp, "app/discover_view.hpp")
assert includes(discover_view_cpp, "app/page_support.hpp")
assert includes(updates_view_cpp, "app/page_support.hpp")
for name in (
    "display_size", "one_line", "update_icon_name", "package_record_destroy",
    "detail_row", "record_transaction_history",
):
    assert defines_function(page_support_cpp, name)
    assert not defines_function(discover_view_cpp, name)
    assert not defines_function(updates_view_cpp, name)
for token in ("refresh_updates(", "refresh_updates_internal(", "rebuild_updates("):
    assert token not in discover_view_cpp
for token in ("state->discover.", "refresh_discover(", "rebuild_discover_"):
    assert token not in updates_view_cpp
assert "notify_catalogue_transaction_completed" in discover_view_cpp
assert "notify_transaction_history_changed" in discover_view_cpp
assert "notify_updates_state_changed" in updates_view_cpp
assert "notify_update_transaction_completed" in updates_view_cpp
assert "notify_transaction_history_changed" in updates_view_cpp

# AppCoordinator routes domain events/shell intents; page lifecycle policy is
# isolated in PageRegistry.
for header in ("app/page_registry.hpp", "app/view_intents.hpp", "app/window_state.hpp"):
    assert includes(app_coordinator, header)
for header in (
    "app/discover_view.hpp", "app/updates_view.hpp", "app/updates_controller.hpp",
    "app/system_view.hpp", "app/repair_view.hpp", "app/repository_view.hpp",
    "app/repository_controller.hpp", "app/installed_controller.hpp",
    "app/history_controller.hpp",
):
    assert not includes(app_coordinator, header)
assert "PageDescriptor" not in app_coordinator
assert "PageDescriptor" in page_registry_cpp
for event in (
    "notify_kernel_state_changed", "notify_repository_state_changed",
    "notify_repository_snapshot_changed", "notify_history_state_changed",
    "notify_transaction_history_changed", "notify_updates_state_changed",
    "notify_catalogue_transaction_completed", "notify_update_transaction_completed",
    "notify_repair_state_changed",
):
    assert defines_function(app_coordinator, event)

# The executable shell does not know page implementations.
for header in ("app/discover_view.hpp", "app/updates_view.hpp", "app/updates_controller.hpp"):
    assert not includes(main_cpp, header)
for call in (
    "set_discover_search_query(", "select_update_preset(",
    "destroy_discover_page(", "destroy_updates_page(",
):
    assert call not in main_cpp
assert "set_app_search_query" in main_cpp
assert "destroy_page_state" in main_cpp
# Controller-owned resources are destroyed through the controller API; the
# page registry must not know that Installed currently uses GtkStringList.
assert "destroy_installed_controller" in installed_controller_hpp
assert "destroy_installed_controller" in installed_controller
assert "destroy_installed_controller(&state->installed)" in page_registry_cpp
assert "state->installed.strings" not in page_registry_cpp

# GUI subprocess creation has one argv/GError ownership boundary. Individual
# views declare commands and keep operation-specific policy/progress only.
assert "g_subprocess_newv(" in process_support_cpp
for path in Path("src/app").glob("*.cpp"):
    if path.name != "process_support.cpp":
        assert "g_subprocess_newv(" not in path.read_text()
assert "start_subprocess(" in app

# Discover consumes public controller surfaces instead of backing storage.
for forbidden in (
    "state->updates.inventory.update_records", "state->repositories.records",
    "state->history.records", "state->installed.loaded", "state->history.loaded",
    "state->updates.inventory.updates_loaded", "history_state->history.loaded",
):
    assert forbidden not in discover_view_cpp
# Native update reconciliation, external update discovery and update planning
# belong to the Updates controller. The Updates view owns GTK task lifetime
# and presentation while reconciliation and planning remain controller-owned.
assert "src/app/updates_controller.cpp" in cmake
assert "refresh_updates_data" in updates_controller
assert "plan_updates" in updates_controller
assert "engine.list_updates(" in updates_controller
assert "discover_flatpak_updates(" in updates_controller
assert "discover_cinnamon_updates(" in updates_controller
assert "engine.list_updates(" not in main_cpp
assert "discover_flatpak_updates(" not in main_cpp
assert "discover_cinnamon_updates(" not in main_cpp
assert "refresh_updates_data" in updates_view_cpp

# External ecosystems remain explicit and preserve Flatpak installation scope.
assert "discover_flatpak_updates" in app
assert "discover_cinnamon_updates" in app
assert "apply_flatpak_updates_selected" in app
assert "apply_cinnamon_updates_selected" in app
assert "discover_native_cinnamon_updates" in cinnamon_native
assert "apply_native_cinnamon_updates_selected" in cinnamon_native
assert "extract_zip_safely" in cinnamon_native
assert "CURLOPT_XFERINFOFUNCTION" in cinnamon_native
assert '"cinnamon-spice-updater"' not in external_updates
assert '"flatpak", "uninstall"' in external_updates
assert '"--unused"' in external_updates
assert 'std::string("flatpak:")' in system_catalogue
assert '(user ? "user:" : "system:")' in system_catalogue
assert "installed_flatpaks" in system_catalogue

# Update plans are exact reviewed capabilities. The privileged helper verifies
# repository generation, exact artifacts and any removals before mutation.
assert '"x3|"' in exact_spec
assert "source_fingerprint" in exact_spec
assert "verify_reviewed_repository_state" in helper
assert "verify_downloaded_artifacts" in helper
assert '"--download-only"' in helper
assert '"--no-download"' in helper
assert 'strcmp(argv[1], "apply-plan")' in helper
assert '"--no-remove"' in helper
assert '"--no-install-recommends"' in helper
assert "validate_apt_simulation" in helper
assert 'simulation_arguments.insert(simulation_arguments.begin(), "-s")' in helper
assert "approved_spec_already_satisfied" in helper
assert "INFILTRATOR_NO_CHANGES_REQUIRED" in helper
assert "pending_specs" in helper

assert "introduced an unapproved change" in guard
assert "parse_remove_line" in guard
assert '"remove:"' in guard
assert "approved.remove != actual.remove" in guard
assert "plan_removal(" in transaction
assert "Refusing to remove Essential/Protected package" in transaction
assert "package->protected_package" in transaction
assert "package->held || held(package->id, policy)" in transaction

# Recommends/conflicts/removals are resolved by the native solver, not widened
# later by the privileged executor.
assert "request.install_recommends" in transaction
assert "owner.recommends" in dependency
assert "planned_removals" in dependency
assert "available_candidates" in dependency
assert "conflict_resolution_candidates" in dependency
assert "best_conflict_resolution_candidate" not in dependency
assert "installed_identity_held" in dependency
assert "candidate_architecture_matches" in dependency
assert "installed_architecture_matches" in dependency
assert "candidate_requires_installed_removal" in dependency
assert "result.remove_installed" in dependency
assert "resolution.remove_installed" in transaction

# Candidate selection follows the host's Debian policy and phased-update rules.
assert "/etc/apt/preferences.d" in preferences
assert "release_origin" in preferences
assert "release_archive" in preferences
assert "DebianPolicyStack" in package_policy
assert "providers_" in package_policy
assert "policy.add(phased_updates)" in reconcile
assert "policy.add(host_preferences)" in reconcile
assert "phased-update-percentage" in text("src/engine/debian_package_index.cpp")
assert "std::minstd_rand" in phased
assert "APT::Get::Always-Include-Phased-Updates".lower() in phased.lower()
assert "APT::Get::Never-Include-Phased-Updates".lower() in phased.lower()
assert "preferences.priority_for" not in reconcile
assert "candidate.pin_priority" in candidate
assert "pin_priority INTEGER NOT NULL" in state_store
assert "policy_provider TEXT NOT NULL" in state_store
assert "release_origin TEXT NOT NULL" in state_store

# Repository cache reuse is cryptographically tied to current signed metadata.
assert "cached_release_current" in repository
assert "verify_payload(" in repository
assert "read_cached(" in repository

# Automatic maintenance executes the exact reviewed purge set.
assert '"--purge-removals"' in helper
assert '"--purge-removals"' in maintenance
assert 'remove:" + removal.identity + "="' in maintenance
assert '"/usr/libexec/infiltrator-software-update-helper"' in maintenance
assert '"apt-get",\n                "autoremove"' not in maintenance

# Release and Cinnamon mutations leave durable crash-recovery evidence.
assert "kReleaseJournal" in release_upgrader
assert '"packages-applying"' in release_upgrader
assert '"packages-applied"' in release_upgrader
assert "recover_pending_release" in release_upgrader
assert "rollback_target_sources" in release_upgrader
assert "Previous repository configuration was restored." in release_upgrader
assert "RENAME_EXCHANGE" in cinnamon_native

# D-Bus parsing and self-update hand-off fail closed on incompatible state.
assert "bool parse_action(" in engine_client
assert 'value == "Install"' in engine_client
assert "if (!parsed || item.package_id.empty())" in engine_client
assert "constexpr guint kApiVersion = 5U;" in engine_service
assert '"engine-version"' in engine_service
assert '<method name="Quit"/>' in engine_interface
assert "kRequiredApiVersion = 5U" in engine_client
assert "kRequiredEngineVersion" in engine_client
assert "engine_version_is_compatible_with_client" in engine_client
assert "ensure_engine_identity(" in engine_client
assert "GetConnectionUnixUser" in engine_client
assert "GetConnectionUnixProcessID" in engine_client
assert '"PlanMixedTransaction"' in engine_service
assert 'name="PlanMixedTransaction"' in engine_interface
assert "request.remove_package_ids" in engine_client

# Root/user maintenance state converges and persistent failures back off.
assert "kAttemptStamp" in auto_updater
assert "read_stamp(kAttemptStamp)" in auto_updater
assert "engine.refresh_installed(result->error)" in tray
assert "system_transaction_history_path()" in auto_updater
assert "system_transaction_history_path()" in maintenance
assert "system_on_battery()" in session_updater
assert "system_on_battery()" in auto_updater
for preferences_consumer in (auto_updater, maintenance, cli, helper):
    assert "kSystemSoftwarePreferencesPath" in preferences_consumer
product_sources = "\n".join(
    path.read_text() for path in Path("src").rglob("*.[ch]pp")
    if "src/infiltratr-common/" not in path.as_posix()
)
assert product_sources.count(
    '"/etc/infiltrator-software/automatic-updates.conf"') == 1
assert '"systemd-inhibit"' in auto_updater

# Repository/source mutation remains narrow and authenticated.
assert "set_apt_list_entry_enabled" in source_mutation
assert "set_apt_deb822_entry_enabled" in source_mutation
assert '"set-apt-source-enabled"' in source_helper
assert "safe_existing_apt_source" in source_helper
assert "/etc/apt/sources.list.d" in source_helper
for policy_path in (update_policy_path, source_policy_path):
    defaults = ET.parse(policy_path).getroot().find(".//defaults")
    assert defaults is not None
    assert defaults.findtext("allow_any") == "auth_admin"
    assert defaults.findtext("allow_inactive") == "auth_admin"
    assert defaults.findtext("allow_active") == "auth_admin"
assert "Authentication is required to install, update or remove system software." in policy

# User-facing major surfaces stay operational; exact widget arrangement is
# deliberately not asserted here.
assert "InstalledController installed" in app
assert "create_installed_page" in app
assert "refresh_installed_controller" in app
assert "installed_worker" in installed_controller
assert "RepositoryController repositories" in app
assert "refresh_repository_controller" in app
assert "make_system_page" in app
assert "make_repair_page" in app
assert "create_history_controller_page" in app
assert "discover_install_clicked" in app
assert '"apply-plan"' in app
assert '"pkexec mintsources"' in app
assert "create_snapshot_before_update" in app

# CLI/replacement features that are part of the Mint Update replacement remain
# wired at a capability level rather than a particular UI spelling.
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
assert '"apply-plan"' in release_upgrader
assert "Provides: mintupdate" in control
assert "Conflicts: mintupdate" not in control
assert "Replaces: mintupdate" in control

# Authored raster assets are checked as assets, not by exact GTK construction
# syntax. This preserves the visual contract while allowing implementation
# extraction from main.cpp.
hero_parts = [
    Path(f"src/app/discover_hero_part{index:02d}.inc")
    for index in range(1, 7)
]
assert all(part.is_file() for part in hero_parts)
encoded_hero = "".join(
    part.read_text().strip().strip('"') for part in hero_parts
)
decoded_hero = base64.b64decode(encoded_hero, validate=True)
assert decoded_hero[:2].hex() == "ffd8"
assert decoded_hero[-2:].hex() == "ffd9"
assert len(decoded_hero) > 15000

logo_part = Path("src/app/titlebar_logo.inc")
assert logo_part.is_file()
encoded_logo = logo_part.read_text().strip().strip('"')
decoded_logo = base64.b64decode(encoded_logo, validate=True)
assert decoded_logo[:8].hex() == "89504e470d0a1a0a"
assert len(decoded_logo) > 5000

# Common remains pinned and consumed as a dependency rather than leaked into
# the Software package payload; packaging verification performs the file-level
# check after this script.
assert "INFILTRATR_COMMON_EXPECTED_VERSION" in cmake
assert "INFILTRATR_COMMON_EXPECTED_COMMIT" in cmake
assert "InfiltratrCommon::Common" in cmake
