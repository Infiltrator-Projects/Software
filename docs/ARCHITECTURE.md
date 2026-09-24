<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Architecture

Infiltrator Software is one graphical product with separated internal responsibilities. The historic split between a software catalogue and an update manager is not reproduced as a product boundary.

This document defines the target architecture beginning with the 0.4 line. The 0.3 implementation still contains an APT-backed compatibility layer; that implementation is transitional.

## Architectural rule

Debian repositories and .deb packages are supported data formats and ecosystems. APT command-line programs are not part of the target architecture.

The UI, panel indicator and future tools consume one Infiltrator package-engine API. They do not independently scan packages and they do not invoke package-manager executables.

## Layers

    ┌──────────────────────────────────────────────┐
    │ GTK4 Software UI                             │
    │ XApp update indicator                       │
    │ future graphical/system clients             │
    └───────────────────┬──────────────────────────┘
                        │ typed local API / events
                        ▼
    ┌──────────────────────────────────────────────┐
    │ Infiltrator package engine                   │
    │                                              │
    │ canonical package/application model          │
    │ installed/available state                    │
    │ Debian version and dependency resolution     │
    │ transaction planning                         │
    │ repository health/trust                      │
    │ download and verification                    │
    │ state publication                            │
    └──────────────┬──────────────┬────────────────┘
                   │              │
                   ▼              ▼
       Debian repository      Flatpak/AppStream
          metadata              integrations
                   │
                   ▼
           derived local DB
                   │
                   ▼
         privileged executor
                   │
                   ▼
              dpkg temporarily

## Native shell

The GTK4 shell owns navigation, interaction, accessibility, appearance and rendering.

The shell must be able to present an interactive first window without waiting for package inventory, repository parsing, update calculation, network access, Flatpak enumeration, AppStream loading, dependency resolution or icon downloads.

Inactive pages are created or hydrated lazily.

## Product core

The core owns package/application identity, classifications, channels, installed/available state, repository provenance, transaction requests and transaction plans.

Core types are package-manager-neutral. No core type exposes an APT process, command string or APT-private cache format.

## Package engine

The package engine is the only component that reconciles Debian package/repository state.

It directly understands supported Debian repository metadata and installed dpkg state. It maintains a fast derived database, computes candidate versions, resolves dependencies, builds plans and publishes changes to clients.

The engine does not use apt, apt-get, apt-cache, or APT's private binary cache files as authoritative state.

Package mechanics and distribution policy are separate layers. Debian mechanics
answer questions such as version ordering, dependency validity and repository
format semantics. Candidate permission and precedence flow through an ordered
package-policy stack:

    repository defaults / Debian Release metadata
                    ▲
                    │ fallback
    host compatibility policy (APT preferences today)
                    ▲
                    │ optional higher-precedence override
    Infiltrator distribution policy (as packages migrate)
                    ▲
                    │
              candidate selector

On a current Mint host, the host compatibility provider protects Mint-specific
packages exactly as the host policy requires. That provider is an adapter, not
the permanent definition of Infiltrator policy. As Infiltrator OS takes
ownership of selected packages, an Infiltrator provider can be inserted ahead
of the host provider for only those packages. Everything not yet migrated keeps
the existing host policy, so the transition does not accidentally replace the
working base system.

The 0.3 backends/apt implementation remains legacy migration code until the native engine reaches parity and is deleted.

See [Package Engine](PACKAGE_ENGINE.md).

## Flatpak and AppStream

Flatpak remains a separate ecosystem normalized into the same application model.

The target integration uses structured library/API access rather than spawning the flatpak command for ordinary inventory or mutation.

AppStream remains the application-metadata normalization layer for host and third-party software where appropriate.

## State ownership

One engine owns package/repository reconciliation.

    package engine
         │
      ┌──┼──┐
      ▼  ▼  ▼
    GUI tray future client

Clients subscribe to snapshots and change events. They do not perform duplicate update calculations.

The first shared service slice is implemented as a session D-Bus service at `net.ssmith.infiltrator.software.Engine`. It owns the in-memory view of the latest package-state generation, caches the update calculation once per generation, exposes status/installed/update snapshots and native transaction planning, and emits generation/health changes. It is D-Bus activatable and keeps stale known-good state readable if a subsequent database reload fails. Installed inventory and ordinary Updates already prefer this shared engine when a published generation is available, and the tray subscribes to its state and health signals instead of running a second resolver. The next slice is the native reconciliation publisher that refreshes configured repository sources plus installed dpkg state and atomically publishes complete generations, allowing the remaining APT compatibility inventory and refresh paths to be removed.

The derived state database is disposable. Authoritative state remains repository metadata, configured repository policy and installed package state.

See [State](STATE.md).

## Privilege model

The graphical application never runs as root.

Unprivileged work includes reading cached/derived state, reading repository metadata, resolving versions and dependencies, planning transactions, downloading into a staging area, verifying signatures/checksums and presenting every proposed change.

Privilege is requested only for the narrow operation that actually changes protected system state.

The privileged executor receives a typed, immutable transaction plan tied to a known state generation. It does not accept arbitrary shell commands or free-form package-manager text.

During the Debian-compatibility phase, the final payload application may use dpkg. That boundary is deliberately narrow so dpkg can later be replaced without redesigning the UI or resolver.

## Concurrency

No repository, package or network operation may block the GTK main loop.

The engine owns workers and cancellation. Results are generation-tagged so stale background work cannot overwrite newer state.

The first frame is independent of engine refresh completion.

See [Performance](PERFORMANCE.md).

## Failure model

Failures are typed state, not crashes and not terminal instructions.

The GUI exposes the affected operation, a human-readable explanation, technical details, retry/repair actions where safe and copyable diagnostic information.

Partial state is visibly partial and is never silently represented as complete authoritative state.

## Common

Common 1.19.24 remains the project-family foundation for mechanisms that are genuinely product-neutral: appearance, semantic colours, typography roles, structural metrics and durable generic utilities.

Package formats, dependency resolution, repository semantics and transaction policy remain in Software rather than being pushed into Common merely for reuse.

## Dependency rule

    UI / tray
        │
        ▼
    product core
        │
        ▼
    package engine ────> Debian repository formats
        │               dpkg installed-state format
        │               Flatpak/AppStream integrations
        │
        └──────────────> Common generic facilities

    privileged executor <── immutable resolved transaction
        │
        └──────────────> dpkg during compatibility phase

No dependency points from the core or UI into APT executables.
