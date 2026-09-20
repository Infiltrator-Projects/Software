<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Roadmap

The roadmap describes product milestones, not historical package-manager boundaries. Debian repositories, .deb packages, AppStream and Flatpak remain supported while the implementation moves away from APT command-line programs.

## 0.1 — Read-only foundation

Implemented: one shell with Discover, Installed, Updates, System, Repositories, History and Repair; backend-neutral models; installed inventory; virtualized GTK4 list; tests and main-branch CI.

## 0.2 — Discover and source management

Implemented: asynchronous Infiltrator Repository refresh; first-party metadata validation; canonical icon digest/cache pipeline; offline metadata fallback; AppStream ingestion; Flatpak catalogue integration; merged search/categories/details; source inventory; and integrated Add Source workflow.

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

Initial 0.4 slices implemented: Installed inventory now parses /var/lib/dpkg/status directly in-process, including architecture, Multi-Arch identity, installed version and size, and no longer spawns dpkg-query. The native engine also parses Debian Packages repository indexes directly into structured package/version records, preserving architecture, payload filename/hash/size, Essential/Priority/Multi-Arch state and raw dependency/provides/conflict relationships for the resolver. Debian package-version ordering is now implemented natively, including epochs, upstream/revision ordering, tilde precedence, punctuation ordering, numeric runs and arbitrarily large epochs. Native candidate selection now combines installed state and repository versions with architecture compatibility, source priorities, holds, deterministic tie-breaking and Debian's no-downgrade-unless-priority-exceeds-1000 rule. APT remains temporarily for live repository refresh and dependency transaction planning until later 0.4 slices replace those paths.

See [Package Engine](PACKAGE_ENGINE.md), [Performance](PERFORMANCE.md) and [State](STATE.md).

## 0.5 — Complete graphical package management

Finish ordinary install/remove/update workflows through the native engine.

Required outcomes include Discover install/remove, per-package and selected updates, source enable/disable/removal, Stable/Beta/Alpha policy, repository priority and trust presentation, download/disk impact before authorization, progress and safe cancellation, durable History, and no ordinary workflow requiring a terminal.

## 0.6 — Repair and recovery integration

Complete diagnostics for broken dependency state, interrupted transactions and repository problems.

Integrate an explicit checkpoint service contract. When InfiltratorFS is available, qualifying system transactions can create a pre-change checkpoint and link it to History and recovery.

## Later — Native payload installer

The 0.4/0.5 engine intentionally isolates final .deb payload application behind the privileged executor.

A later milestone may replace dpkg and/or introduce a native package format. That work must not require redesigning Software's UI, catalogue, state database or transaction model.
