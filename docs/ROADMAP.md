<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Roadmap

## 0.1 — Read-only foundation

One shell with Discover, Installed, Updates, System, Repositories, History and Repair; exact Common 1.19.10 pin; backend-neutral models; explicit capabilities; real read-only dpkg installed inventory; virtualized GTK4 list; tests and main-branch CI.

## 0.2 — Discover, system catalogues and source management

Implemented: asynchronous Infiltrator-Repository refresh; first-party application-record validation; canonical icon digest/cache pipeline; offline metadata fallback; host AppStream application ingestion; configured Flatpak catalogue ingestion; merged search/categories/details; APT and Flatpak installed-state reconciliation; live APT/Flatpak source inventory; and an integrated Add Source workflow.

APT source addition is isolated behind a constrained Polkit-authorized helper that writes modern HTTPS-only Deb822 `.sources` records. User Flatpak remotes are added without elevating the GUI. Package installation/removal/update remains deferred until transaction planning is complete.

## 0.3 — Update inventory and read-only planning

Candidate-version comparison, application/system grouping, dependency resolution, install/upgrade/remove plans, download/disk impact and changelog presentation.

## 0.4 — Privileged execution

Minimal privileged executor, exact-plan authorization, staged verification, progress events, durable history and interrupted-transaction diagnosis.

## 0.5 — Advanced repository and channel management

The 0.2 line already provides source inventory and safe source addition. This milestone adds Stable/Beta/Alpha policy, source enable/disable/removal, priority presentation, repository signature/health state and safe channel-transition planning.

## 0.6 — Recovery integration

Repair workflows, checkpoint service contract, InfiltratorFS pre-system-update checkpoints and rollback linkage in History.

No public release is considered functionally complete merely because the shell builds. Packaging begins after the read-only backend milestone is green and its metadata boundaries are proven.
