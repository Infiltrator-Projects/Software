<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Infiltrator Software

Infiltrator Software is the software-management application for the Infiltrator project family. It is designed as a greenfield 2026 replacement for the traditional split between a graphical software catalogue and a separate operating-system update manager.

The application presents one authoritative view of software discovery, installation, removal, updates, system components, repositories, release channels, history and repair while keeping privileged package operations isolated behind backend interfaces.

**Current source version:** 0.1.1  
**Language:** C++17 application/core with native GTK4 Linux shell; C11 Common foundation  
**Shared foundation:** Common 1.19.8  
**Initial package backend:** APT/.deb  
**Licence:** GPL-3.0-or-later

## Engineering ethos

The project starts from the user problem rather than from the historical shape of Linux package-management tools.

A software manager should answer a small set of questions reliably:

- What software is available?
- What is already installed?
- What can be updated?
- What will an operation change before it runs?
- Where did a package come from?
- Can the system recover if an operation is interrupted or fails?
- What application identity, icon and metadata are authoritative?

The user-facing application is therefore one coherent product. Catalogue discovery, package resolution, transaction execution, repository access and system maintenance remain separate subsystems underneath where that separation improves correctness or privilege isolation.

The UI does not encode APT semantics. It consumes a package-backend contract. APT/.deb is the first backend because it matches the current Infiltrator ecosystem; a future native Infiltrator package engine can implement the same contract without replacing the application.

## Product areas

The primary navigation contract is:

- **Discover** — search and browse available applications.
- **Installed** — installed applications, components, versions, source and size.
- **Updates** — application, library, kernel and system updates in one place.
- **System** — kernels, drivers, core components and operating-system packages.
- **Repositories** — configured sources, priorities and release channels.
- **History** — exact install/remove/update operations with versions and timestamps.
- **Repair** — interrupted transactions, broken dependencies and repository inconsistencies.

System-level changes remain visually and operationally distinct from ordinary application updates even though they are presented by the same application.

## Application identity

Infiltrator Software treats package/repository metadata as the authoritative application identity. An application has one stable identity record containing at least:

- package/application ID;
- display name;
- publisher;
- version;
- summary and description;
- icon identity;
- screenshots;
- executable/desktop integration;
- repository/source;
- release channel;
- installed and available state.

The goal is to eliminate the class of presentation failures caused by multiple unrelated icon and metadata caches disagreeing about the same application.

## Transactions

Mutating operations are modelled as transactions.

Before execution, the backend resolves the complete proposed change set. The UI can therefore show packages to install, upgrade or remove, download size, disk-space effect and whether the transaction touches system-critical components.

The application core owns transaction state and presentation. Backend implementations own package-system mechanics and privilege boundaries.

A future InfiltratorFS integration may create a filesystem checkpoint before qualifying system transactions and associate that checkpoint with package history. That integration is deliberately outside the APT-specific backend.

## Release channels

The model supports **Stable**, **Beta** and **Alpha** channels as first-class metadata rather than requiring users to edit repository configuration manually.

Channel policy belongs to the repository/backend layer; the UI exposes the resulting policy coherently at system and package level.

## Appearance

Software uses the Common 1.19.8 appearance contract directly. The user can select **Follow OS**, **Day** or **Night**. Follow OS is the default, resolves through Common's System mode, watches the GTK desktop appearance live and reapplies the Common Day/Night palette when the host theme changes. The selected mode is persisted in the user's configuration using Common's durable atomic-file writer.

Typography, semantic colours and structural metrics come from Common rather than private Software copies.

## Shared foundation

Infiltrator Software uses Infiltratr Common 1.19.8 for project-family mechanisms that are genuinely generic. Product-specific package semantics remain in this repository.

The project follows the same rule as the rest of the family: Common is used when it provides the authoritative generic implementation; functionality is not moved into Common merely to increase reuse.

## Architecture

```text
src/
├── app/                 Native application shell
├── core/                Product model and transaction model
├── backend/             Backend-neutral package-management contracts
├── backends/apt/        Initial APT/.deb implementation
└── infiltratr-common/   Exact Common 1.19.8 gitlink

tests/                   Backend-contract and core regression tests
docs/                    Architecture, metadata, transaction and roadmap contracts
```

The dependency direction is intentional:

```text
UI -> core -> backend contract <- APT backend
            |
            +-> Common generic facilities
```

The UI never calls APT directly.

## Initial milestone

Version 0.1 establishes the architecture rather than pretending to be a finished store. The first milestone is complete when:

1. the application shell exposes the seven product areas;
2. package/application identity is represented once in the core;
3. backend discovery is separate from the UI;
4. APT can enumerate installed package state through the backend contract;
5. transaction planning is read-only and testable before privileged execution is enabled;
6. tests enforce the backend/core boundary;
7. CI builds and tests the exact Common-pinned source.

Write operations are not enabled until transaction planning, error propagation and privilege separation are proven.

## Build direction

The first supported development target is Linux. The desktop shell uses GTK4 through its C API from C++17, avoiding an additional gtkmm runtime/development layer.

The repository pins the exact reviewed Common 1.19.8 release/commit and uses CMake/CTest. Linux releases publish a verified Debian package, deterministic source bundle, licence and checksums from the exact tested main commit; release publication is immutable.

## Repository policy

`main` is the working branch. Development remains main-only. Published release identities will be immutable and tied to the exact tested source commit, consistent with the wider Infiltrator project family.

## Licence

Copyright © 2026 Shannon Smith.

Infiltrator Software is licensed under the GNU General Public License version 3 or, at your option, any later version (`GPL-3.0-or-later`).
