<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Native Debian compatibility engine

The 0.4 package engine replaces APT command-line programs while preserving Debian repository and .deb compatibility.

This is an implementation replacement, not an ecosystem rewrite.

## Non-goals

0.4 does not invent a new package format, abandon Debian repositories, remove .deb support, merge Flatpak into Debian package mechanics, replace dpkg immediately, or require repository owners to publish a new Infiltrator-only format.

## Inputs

The engine consumes supported authoritative sources directly:

- configured Debian/Infiltrator repository definitions;
- signed repository release metadata;
- package indexes;
- installed dpkg package metadata;
- AppStream metadata;
- Flatpak remotes and metadata through structured integration.

It must not require apt, apt-get or apt-cache for normal operation.

APT private binary caches are not an API and are not inputs to the native engine.

## Repository refresh

Repository refresh performs:

1. load configured sources and policy;
2. fetch signed release metadata;
3. verify trust and signatures;
4. select architecture/component indexes;
5. fetch only changed indexes where possible;
6. verify size and digest;
7. parse into a new database generation;
8. atomically publish the generation;
9. notify subscribed clients.

A failed refresh leaves the last verified generation readable and clearly marks freshness/error state.

## Debian package model

The engine must model the fields needed for safe planning, including Package, Version, Architecture, source repository, Filename, Size/checksums, Depends, Pre-Depends, Recommends policy, Provides, Conflicts, Breaks, Replaces, Essential/priority information, installed state and hold/policy state.

The implementation must follow Debian version-ordering semantics rather than lexical or semantic-version comparison.

## Candidate selection

Candidate selection is deterministic and testable.

It considers installed version, available versions, architecture, configured source/channel policy, repository priority, explicit hold/pin policy supported by Software and dependency satisfiability.

The selected candidate and the reason for selection are inspectable through the GUI details view.

## Resolver

The resolver produces a complete transaction graph before authorization.

It distinguishes explicitly requested changes, dependency-induced changes, alternatives/providers, conflicts/removals, unsatisfied constraints and system-critical changes.

The resolver never mutates the system.

## Download and verification

The engine owns package download, caching and cryptographic verification.

Downloads are content-verified before execution. Failed or incomplete objects are never treated as staged packages.

Package cache policy is independent from authoritative package state and can be cleared safely.

## Flatpak

Flatpak remains a distinct provider normalized into the application model.

The target implementation uses libflatpak or an equivalent structured API for inventory, remote state and transactions instead of routine shell-process invocation.

Flatpak dependency/runtime mechanics remain owned by Flatpak.

## Privileged executor

For the Debian-compatibility phase, the engine passes a resolved immutable transaction to the privileged executor.

The executor applies staged .deb payloads through the supported low-level installer boundary and reports structured progress/results.

The long-term native payload installer can replace this boundary without changing the package engine API.

## Migration from 0.3

The first native slice reads installed package state directly from /var/lib/dpkg/status and no longer spawns dpkg-query for Installed inventory. Architecture and Multi-Arch identity are retained in the normalized record.

The second native slice parses Debian Packages indexes directly without an APT process. It preserves package/version/architecture identity, payload filename, SHA-256 and sizes, Essential/Priority/Multi-Arch metadata, and dependency/provides/conflict relationship text. This parser is format-level engine code and is not tied to APT's private caches.

The third native slice implements Debian version comparison inside the engine. It handles epochs without integer-size limits, upstream and Debian revision parts, numeric runs and leading zeroes, tilde-before-end ordering, letter-before-punctuation ordering and the implicit revision 0 used when no Debian revision is present.

The fourth native slice implements deterministic candidate selection. It matches installed package identity and architecture (including architecture-independent all packages), applies configurable per-source priorities, honours explicit holds, prefers the highest-priority source then the newest Debian version, uses deterministic source/file tie-breaking, and refuses downgrades unless the winning priority exceeds 1000.

The fifth native slice adds the resolver core. It parses Debian Depends and Pre-Depends groups, alternatives and version operators; recursively expands dependency graphs with cycle protection; accepts already-installed satisfiers; chooses repository dependencies using source priority and Debian version ordering; resolves versioned virtual Provides; recognises native, explicit and :any architecture qualifiers at the supported Multi-Arch boundary; honours held dependency names; and surfaces unsatisfied dependencies and selected-package Conflicts/Breaks as typed problems. Recommends remain policy rather than hard dependencies, and this resolver is not wired into privileged execution until native repository state and transaction-plan parity are complete.

The sixth native slice implements repository refresh itself. The engine fetches InRelease directly with libcurl and verifies the OpenPGP signature with gpgv using configured keyrings or known system trusted keyrings. If a repository uses detached metadata, Release plus Release.gpg is verified instead. Release SHA-256 entries and exact byte counts are checked before any package index is accepted. Packages.xz, Packages.gz and plain Packages are supported, parsed directly into native package records, and verified uncompressed snapshots can be published atomically into the engine cache. The GUI still uses the transitional APT refresh path until shared engine state is connected.

The seventh native slice implements the durable package-state store. It uses SQLite in WAL mode with an explicit schema version, immutable generations and an atomically switched current-generation pointer. Installed state and normalized repository package versions are written in one transaction; a failed publish rolls back without disturbing the previously readable generation. The store retains the current and immediately previous generations and is intentionally rebuildable from authoritative repository and installed-package inputs.

The eighth native slice implements deterministic transaction planning for install and upgrade operations. A plan is tied to a state generation and source fingerprint, selects requested roots using repository priority and Debian version rules, expands dependencies through the native resolver, rejects unresolved Conflicts/Breaks, honours holds and downgrade policy, distinguishes requested from dependency-induced changes, carries repository/payload provenance and SHA-256 into each item, and calculates total download bytes plus signed installed-size delta. Removal deliberately remains fail-closed until reverse-dependency state is represented in the engine database.

Migration is complete only when normal operation no longer spawns APT programs.

During migration:

- 0.3 APT code is treated as reference behaviour, not the target abstraction;
- parity tests compare native results with known fixtures;
- GUI code is moved to shared engine state before APT code is deleted;
- no new feature may add another direct APT process dependency.

## Acceptance criteria

0.4 package-engine completion requires update inventory without APT executables, repository refresh without APT executables, installed-package inventory without dpkg-query process invocation, complete deterministic update planning fixtures, correct Debian version-ordering tests, dependency/conflict/provider fixtures, repository signature/checksum failure tests, shared state used by GUI and tray, and automated confirmation that normal paths spawn no APT subprocesses.
