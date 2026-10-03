#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Source/authorization invariants extracted from the Verify workflow.
# Keep this exhaustive: it protects privileged transaction boundaries
# as well as functional and UI contracts.
from pathlib import Path
import xml.etree.ElementTree as ET

app_files = sorted(
    list(Path("src/app").glob("*.cpp")) +
    list(Path("src/app").glob("*.hpp")))
app = "\n".join(path.read_text() for path in app_files)
theme = Path("src/app/theme.cpp").read_text()
cmake = Path("CMakeLists.txt").read_text()
transaction = Path("src/engine/debian_transaction.cpp").read_text()
candidate = Path("src/engine/debian_candidate.cpp").read_text()
package_policy = Path("src/engine/package_policy.cpp").read_text()
phased = Path("src/engine/debian_phased_updates.cpp").read_text()
preferences = Path("src/engine/debian_preferences.cpp").read_text()
reconcile = Path("src/engine/debian_reconcile.cpp").read_text()
repository = Path("src/engine/debian_repository.cpp").read_text()
state_store = Path("src/engine/package_state_store.cpp").read_text()
history = Path("src/core/transaction_history.cpp").read_text()
exact_spec = Path("src/core/exact_transaction_spec.cpp").read_text()
history_controller = Path("src/app/history_controller.cpp").read_text()
history_view = Path("src/app/history_view.cpp").read_text()
installed_controller = Path("src/app/installed_controller.cpp").read_text()
installed_inventory = Path("src/app/installed_inventory.cpp").read_text()
repository_controller = Path("src/app/repository_controller.cpp").read_text()
ui_components = Path("src/app/ui_components.cpp").read_text()
helper = Path("src/helper/update_helper.cpp").read_text()
source_helper = Path("src/helper/source_helper.cpp").read_text()
source_mutation = Path("src/sources/source_mutation.cpp").read_text()
guard = Path("src/helper/apt_plan_guard.cpp").read_text()
engine_client = Path("src/client/engine_client.cpp").read_text()
engine_service = Path("src/service/main.cpp").read_text()
engine_interface = Path("data/net.ssmith.infiltrator.software.Engine.xml").read_text()
update_policy_path = Path("data/net.ssmith.infiltrator.software.updates.policy")
source_policy_path = Path("data/net.ssmith.infiltrator.software.policy")
policy = update_policy_path.read_text()
external_updates = Path("src/external/external_updates.cpp").read_text()
system_catalogue = Path("src/catalogue/system_catalogue.cpp").read_text()
cinnamon_native = Path("src/external/cinnamon_spices.cpp").read_text()
session_updater = Path("src/session/main.cpp").read_text()
auto_updater = Path("src/automation/main.cpp").read_text()
maintenance = Path("src/automation/maintenance.cpp").read_text()
dependency = Path("src/engine/debian_dependency.cpp").read_text()
cli = Path("src/cli/main.cpp").read_text()
release_upgrader = Path("src/release/main.cpp").read_text()
tray = Path("src/tray/main.cpp").read_text()
parity = Path("docs/MINTUPDATE_PARITY.md").read_text()

required_rows = [
    line for line in parity.splitlines()
    if line.startswith("| ") and line.rstrip().endswith("| Required |")
]
assert required_rows
assert all(len(line.split("|")) == 5 for line in required_rows)
# The matrix is an audit inventory, not proof that the host has
# completed a real package, Flatpak, Spice and release-upgrade cycle.
# Never infer removal readiness from self-reported row labels.
assert "Removal approved after host validation" not in parity

# Mint Update Manager replacement surfaces must remain wired, not
# merely documented as complete.
assert "discover_flatpak_updates" in app
assert "discover_cinnamon_updates" in app
assert "apply_flatpak_updates" in app
assert "apply_cinnamon_updates_selected" in app
assert "selected_cinnamon_refs" in app
assert "discover_native_cinnamon_updates" in cinnamon_native
assert '"/json/actions.json"' in cinnamon_native
assert "apply_native_cinnamon_updates_selected" in cinnamon_native
assert "extract_zip_safely" in cinnamon_native
assert "CURLOPT_XFERINFOFUNCTION" in cinnamon_native
assert '"cinnamon-spice-updater"' not in external_updates
assert '"flatpak", "uninstall"' in external_updates
assert '"--unused"' in external_updates
assert '"org.gtk.Gtk3theme."' in external_updates
assert "system_on_battery()" in session_updater
assert "system_on_battery()" in auto_updater
assert '"systemd-inhibit"' in auto_updater
assert "Automatic system update refused because systemd-inhibit is unavailable" in auto_updater
assert "evaluate_update_notification" in tray
assert "refresh_schedule_enabled" in tray
assert "hide_tray" in tray
assert "refresh_runtime_inputs" in tray
assert "install_file_monitors" in tray
assert "g_file_monitor_directory" in tray
assert "runtime_file_changed" in tray
assert "installed_executable_changed" in tray
assert "g_timeout_add_seconds(2U, state_tick" not in tray
assert "const std::string override = read_override();" not in tray
assert '"--security-only"' in cli
assert '"--kernel-only"' in cli
assert '"--dry-run"' in cli
assert '"--refresh-cache"' in cli
assert '"--force-confold"' in cli
assert '"--force-confnew"' in cli
assert '"--install-recommends"' in cli
assert '"ignore"' in cli
assert "load_release_info" in release_upgrader
assert "build_plan" in release_upgrader
assert '"apply-plan"' in release_upgrader
assert "mint_mirror_status" in repository_controller
assert '"pkexec mintsources"' in app
assert "fetch_changelog_url" in app
assert "updates_restart_after_verify" in app
assert "create_snapshot_before_update" in app
assert "package_diagnostics" in app or "diagnostic" in app.lower()

# Flatpak catalogue actions preserve installation scope end to end:
# Discover never feeds Flatpak application IDs into the Debian solver.
assert "set_flatpak_application_installed" in app
assert "set_flatpak_application_installed" in external_updates
assert 'std::string("flatpak:")' in system_catalogue
assert '(user ? "user:" : "system:")' in system_catalogue
assert "installed_flatpaks" in system_catalogue
assert "g_get_user_data_dir" in system_catalogue

# Background update state remains convergent across root/user
# processes and persistent failures observe configured backoff.
assert "kAttemptStamp" in auto_updater
assert "read_stamp(kAttemptStamp)" in auto_updater
assert "engine.refresh_installed(result->error)" in tray
assert "system_transaction_history_path()" in auto_updater
assert "system_transaction_history_path()" in maintenance

# Release and Cinnamon mutation paths retain crash-recovery evidence
# instead of depending only on in-process rollback.
assert "kReleaseJournal" in release_upgrader
assert '"packages-applying"' in release_upgrader
assert '"packages-applied"' in release_upgrader
assert "recover_pending_release" in release_upgrader
assert "Target repositories and recovery state were preserved because" in release_upgrader
assert "RENAME_EXCHANGE" in cinnamon_native

ui_vision = Path("docs/UI_VISION.md").read_text()
assert Path("docs/design/software-ui-target.jpg").is_file()
assert "graphical" in ui_vision.lower()

assert "refresh_updates(state, true)" in app
assert "engine.refresh(result->error)" in app
assert "backend.refresh_metadata(result->error)" not in app
assert '"refresh"}' not in app
assert "AptBackend" not in app
assert "DebianInstalledState::read" in installed_inventory
assert "add_library(software-apt STATIC" not in cmake
assert "refresh_updates_internal(state, true, false)" in app
assert 'strcmp(argv[1], "refresh")' not in helper
assert 'run_apt({"update"})' in helper
assert 'strcmp(argv[1], "apply-plan")' in helper
assert '"--no-remove"' in helper
assert '"--no-install-recommends"' in helper
assert "validate_apt_simulation" in helper
# Review-to-mutation identity is bound to both the independently
# reproduced repository generation and the exact downloaded bytes.
assert '"x3|"' in exact_spec
assert "source_fingerprint" in exact_spec
assert "verify_reviewed_repository_state" in helper
assert "verify_downloaded_artifacts" in helper
assert '"--download-only"' in helper
assert '"--no-download"' in helper
assert 'simulation_arguments.insert(simulation_arguments.begin(), "-s")' in helper
assert "introduced an unapproved change" in guard
assert "parse_remove_line" in guard
assert '"remove:"' in guard
assert "approved.remove != actual.remove" in guard
assert "plan_removal(" in transaction
assert "Refusing to remove Essential/Protected package" in transaction
assert "package->protected_package" in transaction
assert "package->held || held(package->id, policy)" in transaction
assert "Authentication is required to install, update or remove system software." in policy

# Automatic maintenance must execute the exact reviewed purge set,
# never safety-check one autoremove and later run an unconstrained one.
assert '"--purge-removals"' in helper
assert '"--purge-removals"' in maintenance
assert 'remove:" + removal.identity + "="' in maintenance
assert '"/usr/libexec/infiltrator-software-update-helper"' in maintenance
assert '"apt-get",\n                "autoremove"' not in maintenance

# Recommends policy is resolved into the reviewed native plan; the
# privileged executor must not independently broaden it.
assert "request.install_recommends" in transaction
assert "owner.recommends" in dependency
assert "planned_removals" in dependency
assert "available_candidates" in dependency
assert "conflict_resolution_candidates" in dependency
assert "best_conflict_resolution_candidate" not in dependency
assert "installed_identity_held" in dependency
assert "candidate_architecture_matches" in dependency
assert "installed_architecture_matches" in dependency
# Release source switching is transactional across a failed package
# mutation, and Preferences must not synchronously block GTK.
assert "rollback_target_sources" in release_upgrader
assert "Previous repository configuration was restored." in release_upgrader
assert "start_preferences_automation_sync" in app
assert "g_subprocess_communicate_utf8_async" in app

# D-Bus transaction parsing fails closed on unknown actions.
assert "bool parse_action(" in engine_client
assert 'value == "Install"' in engine_client
assert "if (!parsed || item.package_id.empty())" in engine_client

# A stale graphical update row must not turn an already-completed
# exact version into a false transaction failure.
assert "approved_spec_already_satisfied" in helper
assert "INFILTRATOR_NO_CHANGES_REQUIRED" in helper
assert "pending_specs" in helper
assert "engine.refresh_installed(result->error)" in app
assert '"RefreshInstalledState"' in engine_client
assert '<method name="RefreshInstalledState">' in engine_interface

# Repeated repository checks reuse only content whose current signed
# Release metadata is byte-identical and whose cached Packages bytes
# still pass the signed SHA-256/size record.
assert "cached_release_current" in repository
assert "verify_payload(" in repository
assert "read_cached(" in repository

# The update inventory must follow the host's Debian candidate policy,
# not merely choose the numerically newest version from every source.
assert "/etc/apt/preferences.d" in preferences
assert "release_origin" in preferences
assert "release_archive" in preferences
assert "DebianPolicyStack" in package_policy
assert "providers_" in package_policy
assert "policy.add(phased_updates)" in reconcile
assert "policy.add(host_preferences)" in reconcile
assert "phased-update-percentage" in Path("src/engine/debian_package_index.cpp").read_text()
assert "std::minstd_rand" in phased
assert "APT::Get::Always-Include-Phased-Updates".lower() in phased.lower()
assert "APT::Get::Never-Include-Phased-Updates".lower() in phased.lower()
assert "preferences.priority_for" not in reconcile
assert "candidate.pin_priority" in candidate
assert "pin_priority INTEGER NOT NULL" in state_store
assert "policy_provider TEXT NOT NULL" in state_store
assert "release_origin TEXT NOT NULL" in state_store
assert '"policy-provider"' in engine_service
assert '"selection-reason"' in engine_service
assert '"Selection policy: " + policy_name' in app
assert '"Recommended"' in app
assert 'return "view-refresh-symbolic";' in app
assert 'image.package-icon { color:' in theme

# Installed state now owns its asynchronous GTK lifetime outside
# the top-level application controller.
assert "InstalledController installed" in app
assert "create_installed_page" in app
assert "refresh_installed_controller" in app
assert "installed_worker" in installed_controller

# System is an operational inventory, not a milestone placeholder.
assert "make_system_page(state)" in app
assert '"COMPONENTS", "0"' in app
assert '"Review system updates"' in app
assert "classify_package_role" in Path("src/core/model.cpp").read_text()
assert "is_system_component" in Path("src/core/model.cpp").read_text()
assert '"System changes stay distinct"' not in app

# Repository inventory owns its asynchronous lifetime outside the
# top-level application shell; main retains only cross-page wiring.
assert "RepositoryController repositories" in app
assert "refresh_repository_controller" in app
assert "mint_mirror_status" in repository_controller

# History owns its own controller/view lifetime while the application
# shell retains only cross-page coordination.
assert "create_history_controller_page" in app
assert "HistoryController history" in app
assert "TransactionHistoryStore" in history_controller
assert "user_transaction_history_path" in history_controller
assert "system_transaction_history_path" in history_controller
assert "create_history_page" in history_view
assert '"history.sqlite3"' in history
assert "CREATE TABLE IF NOT EXISTS transactions" in history
assert "CREATE TABLE IF NOT EXISTS transaction_items" in history
assert "updates_progress" in app
assert "Update transaction" in app
assert '"Authorize"' in app
assert '"Download"' in app
assert '"Verify"' in app
assert "privileged_update_progress_path" in app
assert "update_progress_from_apt_line" in helper
assert '"--progress-token="' in app
assert '"--progress-token="' in helper
assert ".update-transaction-panel" in theme
assert ".update-stage-active" in theme
assert ".nav-badge-active" in theme
assert "docs/MINTUPDATE_PARITY.md" in Path("README.md").read_text()
parity = Path("docs/MINTUPDATE_PARITY.md").read_text()
assert "Linux Mint Update Manager replacement audit" in parity
assert "The source replacement gate is complete for Software 0.3.52" in parity
control = Path("debian/control").read_text()
assert "Provides: mintupdate" in control
assert "Conflicts: mintupdate" not in control
assert "Replaces: mintupdate" in control
dependency = Path("src/engine/debian_dependency.cpp").read_text()
transaction = Path("src/engine/debian_transaction.cpp").read_text()
assert "candidate_requires_installed_removal" in dependency
assert "result.remove_installed" in dependency
assert "resolution.remove_installed" in transaction
assert "Installation finished; checking installed versions and remaining update candidates." in app
assert "finish_update_progress(" in app

assert "GTK_POLICY_ALWAYS" in app
assert (
    '"DETAIL",\n            "Exact versions",\n            "stat-info"' in history_view
)

# Repair must be a working diagnostics/recovery page, not a milestone
# placeholder. Its privileged repair action is deliberately narrow.
assert "make_repair_page(state)" in app
assert "repair_worker(" in app
assert "run_dpkg_audit(" in app
assert "pending_dpkg_update_fragments(" in app
assert '"repair-configure"' in helper
assert '"Repair is explicit"' not in app

# The graphical redesign target is a checked-in product contract.
assert "hero-panel" in ui_components
assert "titlebar-brand" in app
assert "sidebar-brand" not in app
assert "discover-icon-well" in app
assert "history-transaction-card" in history_view
assert "repair-action-panel" in app
assert ".hero-ribbon-a" not in theme
assert ".nav-icon-well" in theme
assert "metrics->control_radius" in theme
assert "metrics->small_radius" in theme
assert "metrics->card_radius" in theme
assert "border-radius: 14px; padding: 8px 12px" not in theme
assert "border-radius: 8px; box-shadow: none" not in theme
assert "border-radius: 11px; padding: 5px" not in theme

# Second visual pass: Discover shortcuts, grouped Updates and the
# Repair health overview are product-level GUI contracts.
assert "category-shortcuts" in app
assert "make_discover_category_shortcut" in app
assert "update-group-header" in app
assert "make_update_group_header" in app
assert "repair-overview" in app
assert "Software health is good" in app
assert ".version-chip-new" in theme
assert ".repair-overview-good" in theme

# Third visual pass: the landing page must behave like a graphical
# software centre rather than a catalogue table with decoration.
# The obsolete Spotlight prototype was retired; the current landing
# surface is the dashboard plus curated Featured Applications panel.
assert "discover_featured_flow" in app
assert "append_curated_featured" in app
assert '"Featured Applications"' in app
assert ".featured-panel" in theme
assert ".featured-card" in theme
assert ".catalogue-section-heading" in theme

# Fourth visual pass: the shell now owns brand/search while Discover
# exposes dashboard navigation before catalogue detail.
assert "global_search" in app
assert '"Search for software, applications, and packages…"' in app
assert "g_base64_decode" in app
assert "gdk_texture_new_from_bytes" in app
assert "gtk_picture_new_for_paintable" in app
assert "GTK_CONTENT_FIT_COVER" in app
assert "hero, -1, 166" in app
assert "min-height: 166px" in theme
assert "discover_hero_part01.inc" in app
assert "discover_hero_part06.inc" in app
assert "gtk_picture_new_for_filename" not in app
assert "gtk_picture_new_for_resource" not in app
assert "discover_hero_artwork_path" not in app
assert "draw_discover_welcome_art" not in app
assert "gtk_drawing_area_set_draw_func" not in app
assert ".welcome-title-accent" not in theme
hero_parts = [
    Path(f"src/app/discover_hero_part{index:02d}.inc")
    for index in range(1, 7)
]
assert all(part.is_file() for part in hero_parts)
encoded_hero = "".join(
    part.read_text().strip().strip('"')
    for part in hero_parts
)
import base64
decoded_hero = base64.b64decode(encoded_hero, validate=True)
assert decoded_hero[:2].hex() == "ffd8"
assert decoded_hero[-2:].hex() == "ffd9"
assert len(decoded_hero) > 15000
assert "make_dashboard_card" in app
assert '"Available Updates"' in app
assert '"System Health"' in app
assert '"Repositories"' in app
assert ".discover-welcome" in theme
assert ".dashboard-card" in theme
assert ".global-search" in theme
assert "gtk_overlay_new" in app
assert "gtk_label_set_markup" in app
assert '"welcome-artwork-veil"' in app
assert '"titlebar_logo.inc"' in app
assert "kTitlebarLogoBase64" in app
assert '"titlebar-logo"' in app
assert ".welcome-artwork-veil" in theme
assert ".welcome-title" in theme
assert ".titlebar-logo" in theme

# Software title-bar parity: follow the System Settings shell exactly
# except for Software's authored raster identity mark and product copy.
assert '"Software",\n            "titlebar-title"' in app
assert '"Infiltrator OS",\n            "titlebar-subtitle"' in app
assert '"Infiltrator Software",\n            "titlebar-title"' not in app
assert '"titlebar-header-end"' in app
assert ".titlebar-header-end" in theme
assert "state->global_search, 200, -1" in app
assert "Search | Minimize | Maximize | Close" in app
assert "gtk_header_bar_pack_end" in app
assert "global-search-shell" not in app
assert "search-shortcut" not in app
assert "titlebar-button" not in app
assert "titlebar-window-separator" not in app
assert "linear-gradient(to right, #06131f, #08263a)" not in theme
assert "linear-gradient(105deg, #2f67ff, #5137d8)" not in theme
assert '<< ".nav-row:selected { background: " << select_bg' in theme
assert '<< ".sidebar { background: " << panel' in theme
assert "infiltratr_ascii_contains_ci" in theme
assert '"shell-header"' in app
assert '"app-shell"' in app
assert "transitional APT compatibility planner" not in app
assert "theme_button" not in app
assert "min-height: 58px" in theme
assert "button.window-control { min-width: 30px; min-height: 30px" in theme
assert "legacy CI marker" not in app
assert "legacy CI text marker" not in theme
assert '.nav-discover image' not in theme
assert '.page-updates .card' not in theme
assert "nav-discover" not in app

# The title identity is an authored raster mark: exactly the new
# two-slash artwork, never the old three procedural GtkBox bars.
logo_part = Path("src/app/titlebar_logo.inc")
assert logo_part.is_file()
encoded_logo = logo_part.read_text().strip().strip('"')
decoded_logo = base64.b64decode(encoded_logo, validate=True)
assert decoded_logo[:8].hex() == "89504e470d0a1a0a"
assert len(decoded_logo) > 5000
assert "titlebar-mark-bar" not in app
assert ".titlebar-mark-bar" not in theme

# Sidebar convergence: semantic icons and Common-driven selection,
# real update badge and an actual Preferences entry.
assert '"go-home-symbolic"' in app
assert '"view-grid-symbolic"' in app
assert '"view-refresh-symbolic"' in app
assert '"drive-multidisk-symbolic"' in app
assert '"applications-engineering-symbolic"' in app
assert "nav_updates_badge" in app
assert '"Settings"' in app
assert "settings_clicked" in app
assert '"NAVIGATE"' not in app
assert "nav-chevron" not in app
assert ".nav-badge" in theme
assert "button.sidebar-settings" in theme
assert '<< ".nav-row:hover { background: " << surface_hover' in theme

# Sixth visual pass: Discover now follows the target's dashboard
# composition with featured cards, a right rail and health banner.
assert "discover_featured_flow" in app
assert "make_featured_card" in app
assert '"Featured Applications"' in app
assert "discover_update_preview" in app
assert "discover_repository_preview" in app
assert "discover_activity_preview" in app
assert '"Repository Status"' in app
assert '"Recent Activity"' in app
assert '"Keep your system healthy"' in app
assert ".featured-card" in theme
assert "featured_fallback_records" in app
assert "append_curated_featured" in app
assert '"firefox"' in app
assert '"libreoffice"' in app
assert '"gimp"' in app
assert '"vlc"' in app
assert '"featured-card-raster"' in app
assert ".featured-card-raster" in theme
assert ".featured-card-art-shade" in theme
assert ".preview-row" in theme
assert "button.health-banner" in theme
assert ".discover-main-dashboard" in theme
assert "gtk_grid_set_column_homogeneous" in app
assert "left_column,\n        0, 0, 2, 1" in app
assert "right_column,\n        2, 0, 1, 1" in app
assert "make_navigation(state),\n        0, 0, 1, 1" in app
assert "stack,\n        1, 0, 5, 1" in app

# Window usability: explicit native controls and a real page
# scrollbar must remain functional as the dashboard grows.
assert "gtk_window_set_resizable(GTK_WINDOW(window), true)" in app
assert "gtk_header_bar_set_show_title_buttons" in app
assert "G_CALLBACK(maximize_clicked)" in app
assert "gtk_window_maximize" in app
assert "gtk_window_unmaximize" in app
assert "gtk_window_minimize" in app
assert "gtk_window_close" in app
assert '"page-scroller"' in app
assert "GTK_POLICY_ALWAYS" in app
assert "gtk_scrolled_window_set_overlay_scrolling" in app
assert ".page-scroller scrollbar slider" in theme

# The footer must report the exact pinned Common header version,
# never a stale hand-written version string.
assert "INFILTRATR_COMMON_VERSION" in app
assert '"Common 1.19.10"' not in app

# Repositories must be an operational settings surface. Mutable APT
# and Flatpak sources expose a real Enabled/Disabled action, while
# system APT edits stay behind the constrained source helper.
assert "source_toggle_clicked" in app
assert "button.source-state-toggle.state-installed label" in theme
assert "button.source-state-toggle.state-available label" in theme
assert '"set-apt-source-enabled"' in source_helper
assert "safe_existing_apt_source" in source_helper
assert "/etc/apt/sources.list.d" in source_helper
assert "set_apt_list_entry_enabled" in source_mutation
assert "set_apt_deb822_entry_enabled" in source_mutation

# Self-update hand-off is directional. An older GUI/tray may finish
# against a newer engine with a compatible API, while a newly
# installed client must recycle an older resident engine.
assert "constexpr guint kApiVersion = 5U;" in engine_service
assert '"engine-version"' in engine_service
assert '<method name="Quit"/>' in engine_interface
assert "kRequiredApiVersion = 5U" in engine_client
assert "kRequiredEngineVersion" in engine_client
assert "engine_version_is_compatible_with_client" in engine_client
assert "kEngineRestartAttempts = 200U" in engine_client
assert '"PlanMixedTransaction"' in engine_service
assert 'name="PlanMixedTransaction"' in engine_interface
assert "request.remove_package_ids" in engine_client
assert "ensure_engine_identity(" in engine_client
assert "GetConnectionUnixUser" in engine_client
assert "GetConnectionUnixProcessID" in engine_client
assert 'execl(' in app
assert '"/usr/bin/infiltrator-software"' in app

# Discover must remain an operational planned-install surface, not a
# catalogue that permanently advertises an unavailable Install path.
assert "Installation remains disabled until the transaction planner" not in app
assert "discover_install_clicked" in app
assert '"apply-plan"' in app
assert "state->pending_update_plan = plan" in app
assert "plan_removal(" in transaction

# Privileged mutations must authenticate rather than being denied
# merely because the desktop is remote/inactive. Never cache this
# authorization because helper arguments are transaction-specific.
for policy_path in (update_policy_path, source_policy_path):
    defaults = ET.parse(policy_path).getroot().find(".//defaults")
    assert defaults is not None
    assert defaults.findtext("allow_any") == "auth_admin"
    assert defaults.findtext("allow_inactive") == "auth_admin"
    assert defaults.findtext("allow_active") == "auth_admin"
