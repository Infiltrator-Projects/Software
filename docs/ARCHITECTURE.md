<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Architecture

Infiltrator Software is one user-facing application with deliberately separated internal responsibilities. The historic separation between a software catalogue and an update manager is not reproduced as a product boundary.

## Layers

The native shell owns navigation, interaction, accessibility, platform appearance detection and rendering. It never invokes APT, dpkg or another package engine directly.

The core owns package/application identity, classification, channels, installed/available state, transaction requests and transaction plans. These types are package-manager-neutral.

A backend translates one native package ecosystem into the core model. Capabilities are explicit so the UI never assumes that a backend supports mutation merely because it can inventory packages.

APT/.deb is the first package backend. Installed-package inventory remains read-only. Discover is deliberately separate: a catalogue-source contract reads authoritative application records from Infiltrator-Repository, verifies icon digests, caches metadata atomically for offline use and merges local installation state by canonical package identity. Update calculation and transaction planning remain required before any write path.

Common 1.19.10 owns reusable project-family facilities such as semantic theme design and other product-neutral mechanisms. Software's appearance controller consumes Common's System/Day/Night mode policy, semantic palettes, typography roles and structural metrics. Follow OS listens for GTK desktop-theme changes and resolves System dynamically. The selected mode is stored atomically through Common's POSIX durability API. Package semantics remain local to Software.

## Dependency rule

```text
native shell
    |                 |
    v                 v
product core      catalogue source <---- Infiltrator Repository
    |
    v
backend contract <---- APT implementation
    |
    +---- Common product-neutral facilities
```

No dependency points from the core into APT.

## Privilege model

The graphical process is not intended to run as root. Read-only inventory remains unprivileged.

Future mutation is split into an unprivileged planning phase, an explicit user authorization step, a minimal privileged transaction executor, and unprivileged verification/history presentation.

The privileged component will accept a validated transaction description rather than arbitrary shell text.

## Concurrency

Repository/network operations must not block the GTK main loop. Discover uses a two-stage GTask pipeline: catalogue metadata and installed-state reconciliation publish first, then icon verification/caching runs as a separate generation-checked background task. This prevents slow or failed icon downloads from withholding the catalogue UI. Live HTTPS metadata is atomically cached; a network failure can fall back to the last valid cached document without converting cache data into an authoritative source.

## Failure model

Backend failures are data, not crashes. Every operation returns either a typed result or an explicit error. Partial state must not be silently represented as authoritative complete state.
