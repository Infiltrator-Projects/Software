<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Linux Mint Update Manager replacement audit

This file is a replacement acceptance gate, not a feature wish list.

Audit baseline: Linux Mint `linuxmint/mintupdate` master at commit
`a6a9f5767905b8fc94923d0b4ca0538a93309a90` (2026-09-12), version line
7.1.5. The audit covers the GUI, APT backend, Flatpak updater, Cinnamon-spice
integration, preferences, automation, tray behaviour, release-upgrade helper
and command-line interface.

This table records source-level implementation claims. A passing build and this
table do not establish behaviour on a Linux Mint host. Each Required path needs
an integration check on an installed system before removal can be recommended.

## Parity matrix

| Mint Update Manager capability | Software state | Requirement |
| --- | --- | --- |
| Discover APT/Debian updates from configured repositories | Implemented | Required |
| Refresh repository metadata without freezing the GUI | Implemented | Required |
| Select individual updates, Select All and Clear Selection | Implemented | Required |
| Select individual Flatpak and Cinnamon updates in the unified update list | Partial: Flatpak refs can be selected individually in their user/system installation; Cinnamon still applies all available Spices | Required |
| Show installed and target versions | Implemented | Required |
| Show package origin/source and planned download size | Implemented | Required |
| Resolve dependencies before authorization | Implemented, stricter than Mint | Required |
| Review the complete transaction before privilege escalation | Implemented | Required |
| Install selected updates | Implemented | Required |
| Graphical live transaction state during authorization/download/install/configure/verify | Implemented since 0.3.46 | Required |
| Visible Flatpak and Cinnamon transaction state | Partial: phase messages, spinner and active Updates badge are present; transfer and per-item progress are not yet reported | Required |
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
| Cinnamon applet/desklet/theme/extension/Nemo-action update discovery and execution | Partial: the four Spice types supported by cinnamon-spice-updater are discoverable; Nemo actions need a separate inventory path | Required on Cinnamon |
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
| Point-release / distribution release upgrade workflow | Partial: edition normalization, edition meta-package prerequisite and release-notes acknowledgement added; a real upgrade has not been validated | Required before replacing Mint Update Manager on Mint |
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

Removal is not yet approved. Software 0.3.49 was published with a parity claim
based on static source assertions; the claim did not include full host-level
validation. The release upgrade path needed further work after publication.
The corrected source still requires an installed-host test of APT, Flatpak,
Cinnamon Spice, unattended scheduling, tray feedback, failure handling and a
release upgrade before a replacement claim can be made.

This acceptance does not mean Software must copy Mint's implementation. Several
paths are deliberately stricter: package mutation is resolved into an exact
reviewed transaction before privilege escalation; implicit Recommends are not
allowed to silently broaden it; unattended system mutation fails closed when
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
