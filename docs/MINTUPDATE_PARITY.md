<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Linux Mint Update Manager replacement audit

This file is a replacement acceptance gate, not a feature wish list.

Audit baseline: Linux Mint `linuxmint/mintupdate` master at commit
`a6a9f5767905b8fc94923d0b4ca0538a93309a90` (2026-09-12), version line
7.1.5. The audit covers the GUI, APT backend, Flatpak updater, Cinnamon-spice
integration, preferences, automation, tray behaviour, release-upgrade helper
and command-line interface.

This table records the audited replacement contract. Required source paths are
implemented in Software 0.3.52 and exercised by build, unit, smoke and package
verification. Installed-host qualification remains important for destructive
distribution upgrades, but it is no longer a missing feature in the replacement
implementation.

## Parity matrix

| Mint Update Manager capability | Software state | Requirement |
| --- | --- | --- |
| Discover APT/Debian updates from configured repositories | Implemented | Required |
| Refresh repository metadata without freezing the GUI | Implemented | Required |
| Select individual updates, Select All and Clear Selection | Implemented | Required |
| Select individual Flatpak and Cinnamon updates in the unified update list | Implemented: Flatpak refs and individual Cinnamon applets, desklets, extensions, themes and Nemo actions are independently selectable | Required |
| Show installed and target versions | Implemented | Required |
| Show package origin/source and planned download size | Implemented | Required |
| Resolve dependencies before authorization | Implemented, stricter than Mint | Required |
| Review the complete transaction before privilege escalation | Implemented | Required |
| Install selected updates | Implemented | Required |
| Graphical live transaction state during authorization/download/install/configure/verify | Implemented since 0.3.46 | Required |
| Visible Flatpak and Cinnamon transaction state | Implemented: persistent activity state, per-item progress, Cinnamon byte/percentage download progress and streamed Flatpak transaction output | Required |
| Keep update activity visible when the user changes pages | Implemented through persistent transaction state and active Updates badge | Required |
| Durable update/install/remove history | Implemented | Required |
| Tray indicator for checking/errors/updates and opening Updates | Implemented | Required |
| Detect package-state/repository changes and refresh update inventory | Implemented through engine generations/signals and the configured tray refresh schedule | Required |
| Classify kernel/system/application/library/runtime updates | Implemented | Required |
| Ubuntu phased-update policy | Implemented natively | Required |
| Repository/source management | Implemented | Required |
| Broken-package audit and constrained recovery | Implemented | Required |
| Ignore/blacklist packages, with wildcard and optional version matching | Implemented with persistent source-package rules, per-update ignore UI and CLI/system-wide rules | Required |
| Security-update classification and security-only selection/filtering | Implemented from signed Ubuntu/Debian release metadata plus Mint browser-source rules | Required |
| Flatpak update discovery, runtime updates and update execution | Implemented for user and system installations, including applications and runtimes | Required |
| Cinnamon applet/desklet/theme/extension/Nemo-action update discovery and execution | Implemented natively in C++ for all five Cinnamon/Nemo update classes; no cinnamon-spice-updater dependency remains | Required on Cinnamon |
| Automatic package updates | Implemented through the root systemd timer and exact reviewed package plan | Required |
| Automatic Flatpak updates | Implemented through the recurring Cinnamon-session updater | Required when Flatpak is enabled |
| Automatic Cinnamon-spice updates | Implemented through the recurring Cinnamon-session updater | Required on Cinnamon |
| Battery-aware suppression of unattended system updates | Implemented for system, Flatpak and Cinnamon unattended paths | Required |
| Configurable first-refresh and recurring-refresh schedule | Implemented and consumed by tray, session updater and root automatic updater | Required |
| Update-age/security notification policy and notification throttling | Implemented with persistent tracker, grace period, age/day thresholds and notification spacing | Required |
| Reboot-required detection and persistent user indication | Implemented from reboot-required markers and requesting package list | Required |
| Update details: description plus complete binary package list | Implemented using native repository description and source-package grouping | Required |
| Changelog retrieval/display | Implemented for Debian/Ubuntu archive changelogs and Launchpad PPA change records | Required |
| PPA/third-party source information in update details | Implemented from signed repository origin/site provenance and Launchpad PPA identity | Required |
| Self-update handling/restart after Software itself is updated | Implemented: compatible newer engines are accepted during the hand-off, verified state refresh completes, the GUI exec-replaces itself with the installed binary, and a new client recycles any older resident engine | Required |
| dpkg/package-manager lock detection with clear user-facing wait state | Implemented with fcntl lock detection and visible timed wait/retry telemetry | Required |
| Broken APT/source configuration detection with guided repository repair | Implemented through Repair, source inventory diagnostics and repository/mirror guidance | Required |
| Mirror reachability/default-mirror checks and guided mirror switching | Implemented for Linux Mint repository freshness/reachability with direct Mint mirror settings hand-off | Required while hosted on Mint/Ubuntu |
| Launch/manage system snapshots before risky updates | Implemented with Timeshift launch plus optional mandatory pre-system-update snapshot | Required until Infiltrator checkpoint replacement is complete |
| Point-release / distribution release upgrade workflow | Implemented: edition normalization/meta-package prerequisite, release notes/risk acknowledgement, exact reviewed plan, shutdown/sleep inhibition, source transition and post-upgrade identity verification; real destructive host execution remains qualification rather than a source gap | Required before replacing Mint Update Manager on Mint |
| Update Manager information/log view | Implemented through History plus graphical package diagnostic log | Required |
| Keyboard shortcuts/help discoverability | Partial; non-blocking polish remains | Desirable |
| Welcome/onboarding screen | Not required as a separate screen; Discover performs onboarding | Equivalent |
| Configurable visible table columns | Not applicable to Software's card/group UI | Equivalent |
| Hide window after updates / tray visibility preferences | Implemented and shared by the main UI/tray | Required for behavioural parity |
| CLI list and upgrade operations | Implemented by `infiltrator-software-cli` | Required for automation parity |
| CLI security-only / kernel-only filters | Implemented | Required |
| CLI ignore list / system blacklist | Implemented, including persistent user rules and system automatic-update ignore rules | Required |
| CLI refresh-cache and dry-run/simulation | Implemented through native refresh/planning with non-mutating dry-run | Required |
| CLI configuration-file conflict policy (keep local / take maintainer) | Implemented through explicit helper conffile policy | Required |
| CLI install-Recommends policy | Superseded by stricter reviewed-plan policy: implicit Recommends may not broaden a transaction; CLI accepts the compatibility option and requires recommended packages to be explicitly planned | Required |
| System shutdown/reboot inhibition during unattended package mutation | Implemented fail-closed with `systemd-inhibit` | Required |
| Remove unused Flatpak runtimes before unattended Flatpak update | Implemented | Required when Flatpak is enabled |
| Match Flatpak theme runtimes to the desktop theme | Implemented | Required when Flatpak is enabled |

## Replacement acceptance

The source replacement gate is complete for Software 0.3.52. Required
Mint Update Manager capabilities in the audited 7.1.5 baseline have an
implemented Software path, including exact Cinnamon/Nemo selection and
execution, external-update progress, tray visibility, history, unattended
updates and the reviewed release-upgrade workflow.

Software 0.3.52 corrects the replacement-planning bug exposed by an installed
0.3.49 client: a candidate that both Conflicts with and Replaces an installed
package is now resolved as an explicit reviewed removal, including dependency
validation against any replacement Provides relationship.

The 0.3.52 Debian package itself deliberately uses `Provides: mintupdate` and
`Replaces: mintupdate` without `Conflicts: mintupdate` so Software 0.3.49 can
bootstrap to the fixed resolver instead of rejecting its own update. Software 0.3.52 is therefore the bootstrap step. After a host is running the
fixed planner, a subsequent replacement release can restore
`Conflicts: mintupdate`; the corrected resolver will then include removal of
the installed Mint Update Manager explicitly in the reviewed transaction while
`Provides: mintupdate` keeps Mint meta-package dependencies satisfied.

This acceptance does not claim that a generic CI runner can prove a destructive
Linux Mint point-release upgrade on every installed host. That operation still
needs normal release qualification on an installed Mint system. It is not a
missing implementation path.

Software remains deliberately stricter in several places: package mutation is
resolved into an exact reviewed transaction before privilege escalation;
implicit Recommends cannot silently broaden it; archive extraction rejects
unsafe Spice paths and symlinks; unattended system mutation fails closed when
shutdown/sleep inhibition cannot be established.

## Source coverage

The audit explicitly reviewed these Mint Update Manager surfaces:

- `usr/lib/linuxmint/mintUpdate/mintUpdate.py`
- `aptUpdater.py`
- `flatpakUpdater.py`
- `preferences.py`
- `mintupdate-cli.py`
- `Classes.py`
- `session_automatic_upgrades.py`
- `system_automatic_upgrades.py`
- `rel_upgrade.py`
- `util.py`
- `com.linuxmint.updates.gschema.xml`
- the current mintupdate and mintupdate-cli man pages
- systemd automation units, XDG autostart surfaces and update-status icons

The July 2026 Mint changes that moved the dedicated kernel window and kernel
cleanup responsibilities to `mintsysadm` are intentionally not treated as
current mintupdate requirements. Kernel update discovery and safe kernel
package transactions remain required in Software.
