<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Software design documents

These documents define the maintained product contracts that implementation work must preserve.

- [Engineering philosophy](PHILOSOPHY.md) — governing engineering and documentation standard.
- [Architecture](ARCHITECTURE.md) — ownership, layers, privilege and dependency boundaries.
- [Package Engine](PACKAGE_ENGINE.md) — native Debian/.deb compatibility without APT command-line programs as package-state authorities.
- [State](STATE.md) — authoritative versus derived package state, generations and shared engine ownership.
- [Transactions](TRANSACTIONS.md) — resolve, authorize, execute, verify, history and recovery.
- [Performance](PERFORMANCE.md) — first-frame, main-thread, lazy-loading and shared-work requirements.
- [UI Design](UI_DESIGN.md) — graphical-first interaction, page hierarchy, progress and visual semantics.
- [UI Vision](UI_VISION.md) — intended visual/product direction and experience-level constraints.
- [Typography](TYPOGRAPHY.md) — typeface assets, ownership and deterministic packaging requirements.
- [Metadata](METADATA.md) — authoritative application identity and cache rules.
- [Mint Update Parity](MINTUPDATE_PARITY.md) — audited replacement-capability matrix and host-validation status.
- [Release](RELEASE.md) — verification, exact artifact qualification, GitHub publication and branch-state contract.
- [Roadmap](ROADMAP.md) — migration from the 0.3 APT-backed implementation to the native engine and later capabilities.

- [Validation](VALIDATION.md) — canonical build/test commands, privilege-boundary evidence and qualification scope.
