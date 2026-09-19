<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Roadmap

## 0.1 — Read-only foundation

One shell with Discover, Installed, Updates, System, Repositories, History and Repair; exact Common 1.19.10 pin; backend-neutral models; explicit capabilities; real read-only dpkg installed inventory; virtualized GTK4 list; tests and main-branch CI.

## 0.2 — Discover and authoritative first-party identity

Implemented: asynchronous Infiltrator-Repository refresh, first-party application-record validation, canonical icon digest/cache pipeline, offline metadata fallback, search, categories, application details, installed-state merging and transitional-package suppression at the repository catalogue layer.

Still deliberately deferred from this milestone: third-party AppStream/package normalization and all install/remove mutation. Those must not dilute first-party identity correctness or bypass transaction planning.

## 0.3 — Update inventory and read-only planning

Candidate-version comparison, application/system grouping, dependency resolution, install/upgrade/remove plans, download/disk impact and changelog presentation.

## 0.4 — Privileged execution

Minimal privileged executor, exact-plan authorization, staged verification, progress events, durable history and interrupted-transaction diagnosis.

## 0.5 — Repository and channel management

Stable/Beta/Alpha policy, source enable/disable, priority presentation, repository signature/health state and safe channel-transition planning.

## 0.6 — Recovery integration

Repair workflows, checkpoint service contract, InfiltratorFS pre-system-update checkpoints and rollback linkage in History.

No public release is considered functionally complete merely because the shell builds. Packaging begins after the read-only backend milestone is green and its metadata boundaries are proven.
