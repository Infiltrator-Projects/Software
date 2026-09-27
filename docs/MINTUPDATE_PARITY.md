<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Linux Mint Update Manager replacement audit

This file is a replacement acceptance gate, not a feature wish list.

Audit baseline: Linux Mint `linuxmint/mintupdate` master at commit
`a6a9f5767905b8fc94923d0b4ca0538a93309a90` (2026-09-12), version line
7.1.5. The audit covers the GUI, APT backend, Flatpak updater, Cinnamon-spice
integration, preferences, automation, tray behaviour, release-upgrade helper
and command-line interface.

Software must not be declared a complete Mint Update Manager replacement while
any **Required** row below is Missing or Partial.

## Parity matrix

| Mint Update Manager capability | Software state | Requirement |
| --- | --- | --- |
| Discover APT/Debian updates from configured repositories | Implemented | Required |
| Refresh repository metadata without freezing the GUI | Implemented | Required |
| Select individual updates, Select All and Clear Selection | Implemented | Required |
| Show installed and target versions | Implemented | Required |
| Show package origin/source and planned download size | Implemented | Required |
| Resolve dependencies before authorization | Implemented, stricter than Mint | Required |
| Review the complete transaction before privilege escalation | Implemented | Required |
| Install selected updates | Implemented | Required |
| Graphical live transaction state during authorization/download/install/configure/verify | Implemented in 0.3.46 | Required |
| Keep update activity visible when the user changes pages | Implemented in 0.3.46 through active Updates badge | Required |
| Durable update/install/remove history | Implemented | Required |
| Tray indicator for checking/errors/updates and opening Updates | Implemented | Required |
| Detect package-state/repository changes and refresh update inventory | Implemented through engine generation/signals and scheduled refresh | Required |
| Classify kernel/system/application/library/runtime updates | Implemented | Required |
| Ubuntu phased-update policy | Implemented natively | Required |
| Repository/source management | Implemented | Required |
| Broken-package audit and constrained recovery | Implemented | Required |
| Ignore/blacklist packages, with wildcard and optional version matching | Implemented in 0.3.47 with persistent source-package rules and per-update ignore UI | Required |
| Security-update classification and security-only selection/filtering | Implemented in 0.3.47 from signed repository release metadata plus Mint browser-source rules | Required |
| Flatpak update discovery, runtime updates and update execution | **Partial**: remotes/catalogue exist; updater parity is incomplete | Required |
| Cinnamon applet/desklet/theme/extension/Nemo-action update discovery and execution | **Missing** | Required on Cinnamon |
| Automatic package updates | **Missing** | Required |
| Automatic Flatpak updates | **Missing** | Required when Flatpak is enabled |
| Automatic Cinnamon-spice updates | **Missing** | Required on Cinnamon |
| Battery-aware suppression of unattended system updates | **Missing** | Required |
| Configurable first-refresh and recurring-refresh schedule | **Partial**: persistent schedule preferences/UI implemented in 0.3.47; tray/background scheduler consumption remains | Required |
| Update-age/security notification policy and notification throttling | **Missing** | Required |
| Reboot-required detection and persistent user indication | Implemented in 0.3.47 from reboot-required markers and requesting package list | Required |
| Update details: description plus complete binary package list | Implemented in 0.3.47 using native repository description and source-package grouping | Required |
| Changelog retrieval/display | **Missing** | Required |
| PPA/third-party source information in update details | Implemented in 0.3.47 from signed repository origin/site provenance | Required |
| Self-update handling/restart after Software itself is updated | **Missing** | Required |
| dpkg/package-manager lock detection with clear user-facing wait state | Implemented in 0.3.47 with fcntl lock detection and visible timed wait/retry telemetry | Required |
| Broken APT/source configuration detection with guided repository repair | **Partial**: Repair/repository health exists; Mint-style mirror guidance is incomplete | Required |
| Mirror reachability/default-mirror checks and guided mirror switching | **Missing** | Required while hosted on Mint/Ubuntu |
| Launch/manage system snapshots before risky updates | **Missing** | Required until Infiltrator checkpoint replacement is complete |
| Point-release / distribution release upgrade workflow | **Missing** | Required before replacing Mint Update Manager on Mint |
| Update Manager information/log view | **Partial**: History and status exist; live diagnostic log view is incomplete | Required |
| Keyboard shortcuts/help discoverability | **Partial** | Desirable |
| Welcome/onboarding screen | Not required as a separate screen; Discover performs onboarding | Equivalent |
| Configurable visible table columns | Not applicable to Software's card/group UI | Equivalent |
| Hide window after updates / tray visibility preferences | **Partial**: hide-after-update implemented in 0.3.47; shared tray visibility preference awaits tray integration | Required for behavioural parity |
| CLI list and upgrade operations | **Missing** | Required for automation parity |
| CLI security-only / kernel-only filters | **Missing** | Required |
| CLI ignore list / system blacklist | **Missing** | Required |
| CLI refresh-cache and dry-run/simulation | **Partial**: engine planning is non-mutating but no parity CLI | Required |
| CLI configuration-file conflict policy (keep local / take maintainer) | **Missing** | Required |
| CLI install-Recommends policy | **Missing** | Required |
| System shutdown/reboot inhibition during unattended package mutation | **Missing** | Required |
| Remove unused Flatpak runtimes before unattended Flatpak update | **Missing** | Required when Flatpak is enabled |
| Match Flatpak theme runtimes to the desktop theme | **Missing** | Required when Flatpak is enabled |

## Replacement rule

Removing Mint Update Manager from an Infiltrator/Mint installation is blocked
until every Required row is Implemented or explicitly superseded by a stronger
Infiltrator mechanism with tests proving equivalent user-facing behaviour.

A stronger replacement is allowed. A missing capability is not.

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
- `com.linuxmint.updates.gschema.xml`
- the current mintupdate and mintupdate-cli man pages

The July 2026 Mint changes that moved the dedicated kernel window and kernel
cleanup responsibilities to `mintsysadm` are intentionally not treated as
current mintupdate requirements. Kernel update discovery and safe kernel
package transactions remain required in Software.
