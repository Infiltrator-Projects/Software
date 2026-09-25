<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Package state and database contract

Software requires one coherent source of derived package state.

The GUI and panel indicator must not independently rebuild repository, installed-package or update state.

## Authority versus derived state

Authoritative inputs are configured repository policy, verified repository metadata, installed package state, Flatpak authoritative state and first-party release/application metadata.

The Infiltrator package database is derived from those inputs.

The database exists for speed, indexing, coherent snapshots and cross-client sharing. It can be rebuilt.

## Storage

The system package engine owns system-wide derived state under the Infiltrator state namespace, conceptually under /var/lib/infiltrator/software/.

User-specific presentation/cache data belongs under standard per-user data/cache locations.

The implementation now uses SQLite in WAL mode with synchronous durable commits. The schema is explicitly versioned, indexed and rebuildable. SQLite is an implementation mechanism rather than package authority.

## Generations

Every successfully reconciled package state is published as a generation.

A generation contains or references repository snapshot identities, package versions and architectures, installed state, candidate state, normalized application metadata, source/provenance, trust state, update classification and freshness timestamps.

Clients read a consistent generation rather than mixing partially updated tables.

## Publication

Refresh builds new derived state without invalidating the current readable generation.

After validation, publication is atomic.

Readers either see the previous complete generation or the new complete generation, never a half-populated state.

## Shared engine service

The package engine exposes current snapshot/generation, state-changed events, refresh requests, transaction planning and health/freshness. Structured per-package execution progress is not yet part of the D-Bus contract; the current privileged bridge exposes visible indeterminate activity and elapsed time in the GUI.

Software and the panel indicator are clients.

The initial D-Bus service is now implemented. It exposes `GetStatus`, `ListInstalled`, `ListUpdates`, `PlanTransaction` and `ReloadState` on `net.ssmith.infiltrator.software.Engine`, with `StateChanged` and `HealthChanged` signals. Package-state database reads are read-only and do not require clients to own or mutate the database. The service caches update state per generation so multiple clients consume the same calculation.

The indicator subscribes to StateChanged and HealthChanged and reads ListUpdates from the same engine generation used by Software. Software's Installed and ordinary Updates paths likewise prefer the service, and engine-backed update transactions use PlanTransaction. Native reconciliation now owns configured-source refresh and generation publication; a compatibility fallback remains only for systems on which no usable native generation is available.

## Startup

Software opens from the latest valid local generation.

A stale generation is still useful presentation state. Freshness is shown separately while the engine reconciles in background.

No network access is required to display the last known catalogue/update state.

## Failure

A failed refresh does not delete the last valid generation.

The engine records refresh failure separately and publishes health/freshness state to clients.

Corrupt derived state is discarded and rebuilt from authoritative inputs.

## Schema evolution

Database schema versions are explicit.

Migrations are transactional. If a migration cannot complete safely, the engine can rebuild derived state rather than corrupting authoritative package information.

## User-specific state

Search history, selected theme, window geometry and other UI preferences do not belong in the system package database.

User Flatpak state may be represented in a per-user engine overlay while preserving the same snapshot/event semantics.

## Security

Unprivileged clients may read presentation state and request plans.

They cannot forge authoritative package state or privileged execution results.

The privileged executor does not accept database rows as implicit authority; it receives a validated immutable transaction tied to a known generation.

## Implemented generation store

The native engine now has a PackageStateStore that publishes installed and repository package state as immutable SQLite generations. Publication uses a single immediate transaction: generation rows, installed records, repository records and the current-generation pointer commit together or not at all. The current and immediately previous generations are retained so readers never need to observe a partially written refresh and a failed publish leaves the previous generation intact.

Schema version 4 retains installed Depends, Pre-Depends, Provides, Priority, Multi-Arch and Essential metadata alongside installed identity/version/size, and repository policy/provenance fields used by native candidate selection. Migrations from earlier derived schemas run inside one SQLite transaction; on failure no partial DDL or version change is retained. Current-generation reads also use one read transaction so a client cannot mix rows from different published generations. Derived state remains rebuildable from authoritative inputs.


## Externally installed first-party native builds

First-party native `.run` installers that produce and install a Debian package
remain ordinary installed-package state. Calendar is the reference case: its
local hardware-native builder emits an `infiltrator-calendar` package with a
version such as `1.0.45+nativepgo1` and installs that package through the
Debian package database.

Software must therefore discover such builds from authoritative dpkg state;
they do not require a separate private installation receipt. Debian candidate
comparison remains authoritative: a later release such as `1.0.46` is newer
than `1.0.45+nativepgo1`, while a same-release generic `1.0.46` is not
allowed to silently downgrade `1.0.46+nativepgo1`.

Regression fixtures cover both installed-state parsing and those version-order
semantics.
