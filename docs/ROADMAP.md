<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Roadmap

The roadmap describes product milestones, not historical package-manager boundaries. Debian repositories, .deb packages, AppStream and Flatpak remain supported while the implementation moves away from APT command-line programs.

## 0.1 — Read-only foundation

Implemented: one shell with Discover, Installed, Updates, System, Repositories, History and Repair; backend-neutral models; installed inventory; virtualized GTK4 list; tests and main-branch CI.

## 0.2 — Discover and source management

Implemented: asynchronous Infiltrator Repository refresh; first-party metadata validation; canonical icon digest/cache pipeline; offline metadata fallback; AppStream ingestion; Flatpak catalogue integration; merged search/categories/details; source inventory; integrated Add Source workflow; and operational graphical enable/disable controls for configured APT sources and Flatpak remotes.

## 0.3 — Operational updates and panel integration

Implemented: available-update inventory, system-critical classification, preflight transaction planning, authenticated update execution, explicit repository refresh and a dedicated XApp panel indicator with update/check/install/error states.

0.3 uses APT command-line programs internally. That is transitional and is the source of avoidable startup/work duplication discovered after the milestone.

## 0.4 — Native Debian compatibility engine and fast shared state

Replace the APT process dependency without changing package formats or repository compatibility.

Required outcomes:

- no apt, apt-get or apt-cache process invocation in normal Software operation;
- direct Debian repository metadata ingestion;
- direct installed-state ingestion from supported dpkg metadata;
- Debian version comparison and candidate selection;
- dependency/provides/conflicts modelling sufficient for safe update and install planning;
- native repository refresh/download/verification;
- one durable derived package-state database;
- one engine shared by Software and the panel indicator;
- no duplicate update scans;
- lazy page hydration;
- first window presented before package/network work;
- structured Flatpak integration rather than routine CLI spawning;
- existing .deb, Debian repository, AppStream and Flatpak functionality retained;
- constrained privileged execution remains isolated from the GUI.

Initial 0.4 slices implemented: Installed inventory now parses /var/lib/dpkg/status directly in-process, including architecture, Multi-Arch identity, installed version and size, and no longer spawns dpkg-query. The native engine also parses Debian Packages repository indexes directly into structured package/version records, preserving architecture, payload filename/hash/size, Essential/Priority/Multi-Arch state and raw dependency/provides/conflict relationships for the resolver. Debian package-version ordering is now implemented natively, including epochs, upstream/revision ordering, tilde precedence, punctuation ordering, numeric runs and arbitrarily large epochs. Native candidate selection now combines installed state and repository versions with architecture compatibility, Release metadata, APT preference/pinning policy from /etc/apt/preferences and /etc/apt/preferences.d, default NotAutomatic/ButAutomaticUpgrades priorities, holds, deterministic tie-breaking and Debian's no-downgrade-unless-priority-exceeds-1000 rule. The first native dependency resolver now parses Depends/Pre-Depends expressions, version constraints and alternatives, resolves recursive dependency graphs with cycle protection, honours installed packages and holds, resolves versioned virtual Provides, applies source policy to dependency candidates, understands basic Multi-Arch qualifiers, and reports unsatisfied dependencies plus selected-package Conflicts/Breaks instead of silently planning an unsafe transaction. Native repository refresh is now implemented at the engine boundary: it downloads Release/InRelease metadata directly with libcurl, verifies OpenPGP repository signatures with configured or system trusted keyrings (including APT-compatible ASCII-armored `.asc` Signed-By keys such as Docker's recommended keyring), validates Release SHA-256 and byte counts before parsing indexes, supports xz/gzip/plain Packages indexes, and atomically publishes verified local cache files. The shared package-state database is now implemented with SQLite WAL generations, transactional atomic publication, current-generation snapshots, two-generation retention and rollback-safe failed publishes. Native install/upgrade transaction planning is now implemented on top of the shared state model: exact roots are selected by repository policy, dependencies are resolved recursively, holds and downgrade rules are enforced, conflicts fail closed, explicit versus dependency-driven changes are retained, and download/disk impact plus payload provenance are calculated against a specific state generation. Native removal planning is now implemented with installed dependency, Pre-Depends, Provides, Priority, Multi-Arch and Essential metadata retained in shared state; removal fails closed when a retained package would lose a hard dependency, and Essential packages cannot be removed through the native planner. The shared engine service is now implemented as a D-Bus-activatable session service with read-only generation loading, cached update calculation, installed/update snapshot APIs, native transaction planning, state/health signals and stale-state retention on reload failure. The first client migration is also implemented: Installed and ordinary Updates inventory prefer the shared engine, engine-backed updates use native transaction planning, Installed hydration runs off the GTK thread, and the panel indicator subscribes to StateChanged/HealthChanged so it consumes the same generation instead of running a second resolver. Native reconciliation now refreshes configured Debian sources, reads direct installed dpkg state and atomically publishes coherent shared generations through the engine RefreshState path. Remaining compatibility code is retained only at explicit transitional execution boundaries while native payload application is still delegated to the constrained installer.

See [Package Engine](PACKAGE_ENGINE.md), [Performance](PERFORMANCE.md) and [State](STATE.md).

## 0.5 — Complete graphical package management

Finish ordinary install/remove/update workflows through the native engine.

Required outcomes include Discover install/remove, per-package and selected updates, source enable/disable/removal, Stable/Beta/Alpha policy, repository priority and trust presentation, download/disk impact before authorization, progress and safe cancellation, durable History, and no ordinary workflow requiring a terminal.

Implemented 0.5 slices: Discover now supports install, update and removal through the same reviewed native transaction flow. Removal carries installed relationship metadata through the shared generation, performs reverse-dependency safety checks, rejects Essential-package removal and revalidates the exact approved mutation set at the privileged boundary. Updates now supports per-package selection, arbitrary selected subsets, Select all/Clear controls and all-updates operation through the same complete native preflight plan. Privileged update execution now remains visibly active with an indeterminate progress indicator and elapsed-time status through metadata refresh, exact-plan revalidation, package application and final state verification. History is operational and persists approved transaction outcomes plus exact before/after package versions and provenance in a per-user SQLite database.

## 0.6 — Repair and recovery integration

Complete diagnostics for broken dependency state, interrupted transactions and repository problems.

Integrate an explicit checkpoint service contract. When InfiltratorFS is available, qualifying system transactions can create a pre-change checkpoint and link it to History and recovery.

## Later — Native payload installer

The 0.4/0.5 engine intentionally isolates final .deb payload application behind the privileged executor.

A later milestone may replace dpkg and/or introduce a native package format. That work must not require redesigning Software's UI, catalogue, state database or transaction model.
