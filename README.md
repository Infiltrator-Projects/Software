<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Infiltrator Software

**Project copyright:** © 1993-2026 Shannon Smith

Infiltrator Software is the software-management application for the Infiltrator project family. It presents software discovery, installation, removal, updates, system components, repositories, release channels, history and repair as one coherent graphical product.

**Current source version:** 0.3.61<br>
**Language:** C++17 application/core with native GTK4 Linux shell; C11 Common foundation  
**Shared foundation:** Common 1.19.36  
**Current package compatibility:** Debian repositories and .deb packages; Flatpak and AppStream catalogue integration  
**0.4 direction:** native Infiltrator Debian-compatibility engine for inventory, refresh and resolution; constrained compatibility execution for final .deb mutation  
**Licence:** GPL-3.0-or-later

## Product principles

The project starts from the user problem rather than from the historical shape of Linux package-management tools.

The application must answer:

- What software is available?
- What is already installed?
- What can be updated?
- What will an operation change before it runs?
- Where did software come from?
- Is the source trusted and healthy?
- Can an interrupted or failed operation be diagnosed and recovered?
- What application identity, icon and metadata are authoritative?

The UI is package-system-neutral. Debian repositories, .deb packages, AppStream and Flatpak remain supported formats and ecosystems. Normal GUI inventory, repository refresh and transaction planning no longer depend on apt, apt-get or apt-cache processes. Read-only package metadata refresh is unprivileged: checking for updates must never ask for administrator credentials. Authorization is reserved for actual system mutation.

The native Infiltrator package engine reads Debian repository metadata directly, applies host compatibility policy including APT preferences and phased-update eligibility when selecting candidates, maintains its own derived package-state database, resolves updates and transactions itself, and uses a constrained privileged executor for final package writes. The privileged compatibility executor still uses the host's low-level Debian package-management boundary for mutation while that final execution layer is being replaced; dpkg remains the .deb payload installer.

## Linux Mint Update Manager replacement status

Software 0.3.52 completes the source-level replacement gate against `linuxmint/mintupdate` commit `a6a9f5767905b8fc94923d0b4ca0538a93309a90` (7.1.5 line). The native C++ Cinnamon backend discovers and updates applets, desklets, extensions, themes and Nemo actions, supports exact per-item selection, reports transfer/item activity, records external updates in Software history, and includes Flatpak/Cinnamon/Nemo state in the tray. The native Debian resolver now models a package that both `Conflicts` with and `Replaces` an installed package as an explicit reviewed removal rather than a fatal conflict. For bootstrap from Software 0.3.49, 0.3.52 deliberately `Provides` and `Replaces` `mintupdate` without declaring a package conflict, so the old planner can install 0.3.52; 0.3.52 is the bootstrap step; after it is installed, a subsequent replacement release can restore the `mintupdate` conflict and the corrected planner will include removal of the installed Mint Update Manager explicitly in the reviewed transaction while `Provides: mintupdate` keeps Mint meta-package dependencies satisfied. `docs/MINTUPDATE_PARITY.md` is the audited capability matrix.

A real distribution point-release upgrade remains an installed-host qualification exercise because CI cannot safely perform a destructive upgrade of the user's machine; that is a validation limitation, not a missing release-upgrade code path.

Self-updates complete without exposing package-engine version plumbing to the desktop: older running clients may finish against a newer compatible engine, the GUI then replaces itself with the newly installed executable, and the new client recycles any older resident engine.

## User experience

Software is graphical first and graphical throughout. Ordinary workflows must not require a terminal.

The primary navigation contract is:

- **Discover** — merged catalogue of verified Infiltrator applications, host AppStream applications and configured Flatpak sources.
- **Installed** — installed applications and components with versions, source, size and state.
- **Updates** — preferred application, library, kernel and system update candidates with per-package/subset selection, visible repository/policy provenance, complete preflight planning before authorization, and visible elapsed activity through privileged execution and final state verification, security-only selection, persistent ignore rules, reboot-required indication and source-package details.
- **System** — live kernel, driver and core operating-system inventory with installed/current versions, preferred update state, system-critical counts, repository refresh and a direct hand-off to the unified Updates workflow.
- **Repositories** — Debian/Infiltrator sources, Flatpak remotes, channels, trust and health, with graphical enable/disable controls for mutable configured sources.
- **History** — live durable transaction history with completed/failed outcomes, exact before/after versions, requested and dependency-driven changes, source provenance and transaction identifiers.
- **Repair** — live package/repository diagnostics, native-state reconciliation, verified repository-state rebuild and constrained completion of interrupted dpkg configuration. Dependency-changing repair plans and filesystem checkpoint rollback remain later recovery work.

Advanced technical information remains available through GUI details views with copyable diagnostics; it is not exposed by forcing the user into a shell.

## Fast-start contract

Opening Software must not trigger package resolution, repository refresh, network access or a complete installed-package scan before the first window is presented.

Startup is:

    process start
        ↓
    construct GTK shell
        ↓
    present interactive window
        ↓
    read cached state
        ↓
    refresh selected page asynchronously
        ↓
    reconcile package/repository state in background

Inactive pages load lazily. The tray and the main window consume the same package-engine state instead of independently calculating updates.

See [Performance](docs/PERFORMANCE.md) and [State](docs/STATE.md).

## Package architecture

The current dependency direction is:

    GTK4 UI ───────────────┐
    XApp panel indicator ──┼──> Infiltrator package engine
    future tools ──────────┘             │
                                         ├── Debian repository reader
                                         ├── local package-state database
                                         ├── dependency/version resolver
                                         ├── transaction planner
                                         ├── downloader/verifier
                                         ├── AppStream integration
                                         └── Flatpak integration
                                                    │
                                                    └── constrained privileged executor
                                                                │
                                                                └── dpkg/.deb compatibility boundary

The GUI and shared engine do not invoke apt, apt-get or apt-cache for normal inventory, refresh or transaction planning. They also do not depend on APT's private binary cache files. The constrained privileged executor remains the compatibility boundary for final Debian package mutation until the native payload installer replaces it.

This is not a package-format rewrite. Debian repositories and .deb packages remain supported.

See [Package Engine](docs/PACKAGE_ENGINE.md).

## Authoritative state

Repository metadata and installed package state remain authoritative. Software maintains a fast, disposable, derived local database for presentation and planning. Deleting the derived database must never destroy authoritative package or repository state; it must be reconstructible.

One background engine owns reconciliation. GUI, panel indicator and future clients subscribe to that shared state.

See [State](docs/STATE.md).

## Transactions

Mutating operations follow explicit phases:

    Refresh → Resolve → Present → Authorize → Stage → Checkpoint
            → Execute → Verify → Record → Recover

The complete proposed change set is resolved before authorization. The UI shows installs, upgrades, removals, download size, disk-space effect and system-critical impact.

A future InfiltratorFS integration can associate a pre-change filesystem checkpoint with qualifying system transactions without coupling filesystem mechanics to Debian package handling.

See [Transactions](docs/TRANSACTIONS.md).

## Appearance

Software uses the Common 1.19.36 appearance contract. Follow OS, Day and Night modes share project-family typography, semantic colours and structural metrics.

Discover is visual and spacious. Installed and Updates are denser working views. System separates critical components clearly. Repositories behaves like a source/settings surface. History is chronological. Repair presents health first and problems only when they exist.

See [UI Design](docs/UI_DESIGN.md) and the [Software UI Vision](docs/UI_VISION.md).

## Architecture

    src/
    ├── app/                 GTK4 application shell and page controllers
    ├── client/              shared package-engine D-Bus client
    ├── core/                package/application and transaction model
    ├── engine/              native Debian state, repository and resolver engine
    ├── catalogue/           Infiltrator + AppStream catalogue sources
    ├── external/            Flatpak and Cinnamon/Nemo update providers
    ├── sources/             repository/source modelling
    ├── helper/              constrained privileged compatibility helpers
    ├── tray/                XApp desktop-panel indicator
    └── infiltratr-common/   exact Common 1.19.36 gitlink

    tests/                   regression and contract tests
    docs/                    architecture and product contracts

## Current and next milestone

0.3/0.3.1 made Updates operational using an APT-backed implementation and added the Mint-style panel status process.

The native-engine migration has now crossed the GUI boundary: Installed, Updates, Discover planning, repository refresh, kernel inventory and the panel indicator use native shared state and native transaction planning. The old GUI `AptBackend` implementation and its backend abstraction have been removed rather than retained as an iterative fallback.

Installed keeps one deliberately small no-process recovery path that reads `/var/lib/dpkg/status` directly if the shared engine is unavailable. It is separated from the package engine so fallback inventory cannot accidentally pull the full resolver/repository stack into the GUI.

Discover install, update and removal workflows are native-planner only. Every operation shows the complete resolved change set before authorization and executes only the exact approved mutations through the constrained privileged helper. Updates uses the same native preflight planner for per-package, arbitrary subset and all-updates operations.

Read-only refresh and planning no longer spawn APT programs. The remaining compatibility layer is the privileged execution boundary: immediately before mutation it refreshes and re-simulates the reviewed exact operation set against current host package state. Installs/upgrades must retain their approved package identities, architectures and exact versions; approved removals must retain their exact installed versions. Any added, missing or changed mutation aborts before package mutation.

The Updates page also avoids the old double external scan: initial hydration discovers Flatpak/Cinnamon state once, while the immediately following repository-metadata refresh reuses that external snapshot instead of repeating Flatpak processes and Cinnamon HTTPS catalogue requests.

## Repository policy

main is the working branch. Development remains main-only. Published release identities are immutable and tied to the exact tested source commit.

## Licence

Copyright © 1993-2026 Shannon Smith.

Infiltrator Software is licensed under the GNU General Public License version 3 or, at your option, any later version (GPL-3.0-or-later).


## Update Manager replacement parity

See `docs/MINTUPDATE_PARITY.md` for the Linux Mint Update Manager replacement audit and removal gate.
