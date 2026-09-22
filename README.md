<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Infiltrator Software

**Project copyright:** © 1993-2026 Shannon Smith

Infiltrator Software is the software-management application for the Infiltrator project family. It presents software discovery, installation, removal, updates, system components, repositories, release channels, history and repair as one coherent graphical product.

**Current source version:** 0.3.11  
**Language:** C++17 application/core with native GTK4 Linux shell; C11 Common foundation  
**Shared foundation:** Common 1.19.24  
**Current package compatibility:** Debian repositories and .deb packages; Flatpak and AppStream catalogue integration  
**0.4 direction:** native Infiltrator Debian-compatibility engine with no apt, apt-get or apt-cache process dependency  
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

The UI is package-system-neutral. Debian repositories, .deb packages, AppStream and Flatpak remain supported formats and ecosystems. APT command-line programs are an implementation detail of the 0.3 line, not part of the product contract. Read-only package metadata refresh is unprivileged: checking for updates must never ask for administrator credentials. Authorization is reserved for actual system mutation.

The 0.4 architecture replaces those APT command invocations with a native Infiltrator package engine. The engine reads Debian repository metadata directly, maintains its own derived package-state database, resolves updates and transactions itself, and uses a constrained privileged executor for final package writes. dpkg remains the temporary .deb payload installer until a later native installer exists.

## User experience

Software is graphical first and graphical throughout. Ordinary workflows must not require a terminal.

The primary navigation contract is:

- **Discover** — merged catalogue of verified Infiltrator applications, host AppStream applications and configured Flatpak sources.
- **Installed** — installed applications and components with versions, source, size and state.
- **Updates** — available application, library, kernel and system updates with complete preflight planning before authorization.
- **System** — kernels, drivers and core operating-system components.
- **Repositories** — Debian/Infiltrator sources, Flatpak remotes, channels, trust and health.
- **History** — durable transaction history with before/after versions and outcomes.
- **Repair** — diagnostics and guided recovery for package, repository and interrupted-transaction problems.

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

The target 0.4 dependency direction is:

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
                                                                └── dpkg (.deb payloads)

The application does not call apt, apt-get or apt-cache in the target architecture. It also does not depend on APT's private binary cache files.

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

Software uses the Common 1.19.24 appearance contract. Follow OS, Day and Night modes share project-family typography, semantic colours and structural metrics.

Discover is visual and spacious. Installed and Updates are denser working views. System separates critical components clearly. Repositories behaves like a source/settings surface. History is chronological. Repair presents health first and problems only when they exist.

See [UI Design](docs/UI_DESIGN.md).

## Architecture

    src/
    ├── app/                 GTK4 application shell
    ├── core/                package/application and transaction model
    ├── catalogue/           Infiltrator + AppStream catalogue sources
    ├── sources/             repository/source modelling
    ├── helper/              constrained privileged helpers
    ├── tray/                XApp desktop-panel indicator
    ├── backend/             backend-neutral compatibility contracts
    ├── backends/apt/        0.3 legacy APT implementation to be retired in 0.4
    └── infiltratr-common/   exact Common 1.19.24 gitlink

    tests/                   regression and contract tests
    docs/                    architecture and product contracts

## Current and next milestone

0.3/0.3.1 made Updates operational using an APT-backed implementation and added the Mint-style panel status process.

0.4 replaces the APT process dependency with the native Debian-compatibility engine, introduces shared package state, removes duplicate update scans, makes page loading lazy, and establishes measurable startup/performance requirements. Existing Debian repositories, .deb packages, AppStream and Flatpak support remain.

The first client cutover is now implemented: Installed, ordinary Updates inventory and native update planning use the shared D-Bus engine when a published generation is available, while the panel indicator subscribes to engine state/health changes instead of owning a second resolver. The direct Debian installed-state reader remains a no-process fallback, and explicit repository refresh/update inventory retains the 0.3 compatibility path until native reconciliation publishes complete source state.

Discover installation is now operational. Selecting Install resolves a complete native transaction when shared state is available, falls back to the transitional APT planner when necessary, shows every resolved package change before authorization, and executes the exact approved package/version set through the constrained privileged helper. The same exact-plan execution path is now used by Updates.

0.3.11 hardens that compatibility boundary. After administrator authorization and the root-owned metadata refresh, Software performs a second non-mutating APT simulation and requires its install/upgrade set to match the approved package identities, architectures and exact versions one-for-one. Any new dependency, missing change, removal or architecture drift aborts before package mutation.

The next 0.4 slice is the native reconciliation publisher that turns configured repository sources plus installed dpkg state into the shared generation automatically. Once that owns refresh end to end, the remaining APT compatibility inventory paths can be deleted rather than merely bypassed.

The 0.4 design is documented before implementation so code cannot accidentally preserve the startup and coupling problems exposed by 0.3.

## Repository policy

main is the working branch. Development remains main-only. Published release identities are immutable and tied to the exact tested source commit.

## Licence

Copyright © 1993-2026 Shannon Smith.

Infiltrator Software is licensed under the GNU General Public License version 3 or, at your option, any later version (GPL-3.0-or-later).
