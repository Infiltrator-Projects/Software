<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Infiltrator Software design documents

These documents define the product contracts that implementation work must preserve.

- [Architecture](ARCHITECTURE.md) — ownership, layers, privilege and dependency boundaries.
- [Package Engine](PACKAGE_ENGINE.md) — native Debian/.deb compatibility without APT command-line programs.
- [State](STATE.md) — authoritative versus derived package state, generations and shared engine ownership.
- [Performance](PERFORMANCE.md) — first-frame, main-thread, lazy-loading and shared-work requirements.
- [UI Design](UI_DESIGN.md) — graphical-first interaction, page hierarchy, progress and visual semantics.
- [Metadata](METADATA.md) — authoritative application identity and cache rules.
- [Transactions](TRANSACTIONS.md) — resolve, authorize, execute, verify, history and recovery.
- [Roadmap](ROADMAP.md) — migration from the 0.3 APT-backed implementation to the native engine and later capabilities.
