<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Authoritative software metadata

Traditional Linux software presentation can derive identity from Debian control fields, desktop files, AppStream, repository catalogues, icon themes and local caches. When those disagree, the user can see missing icons, duplicate names, stale descriptions or a catalogue entry that does not match the installed application.

Infiltrator Software treats that as an identity-integrity problem.

## Canonical record

The normalized identity contains a stable application/package ID, package identity, display name, publisher, installed/available versions, summary, description, icon identity, screenshots, desktop integration, repository/source, channel, software classification, sizes and system-critical state.

## First-party repository contract

For first-party Infiltrator applications, repository metadata is generated from the same immutable release identity that publishes the package.

The pre-install catalogue icon and the desktop icon installed by the package originate from the same release package. The published application record now carries `icon_url` and `icon_sha256`; Discover downloads an icon only over HTTPS, verifies the SHA-256 digest, and caches it by digest. The cache never becomes a competing source of identity.

There is no separate icon-helper package.

Changing an icon creates new release metadata and a new digest. Repository refresh therefore carries the icon change naturally.

## Source precedence

For first-party software:

1. verified Infiltrator repository application record;
2. package control metadata needed for package-system mechanics;
3. installed desktop integration needed for launching.

For third-party packages, a backend may normalize AppStream and package metadata, but provenance remains visible and conflicting identities are not silently merged.

## Duplicate prevention

Aliases and transitional packages are package-mechanics records, not separate applications. The application-level catalogue maps or suppresses them while an advanced package view may still expose the underlying package detail.

## Cache rule

Caches are disposable derived data. Deleting Software's cache must never destroy authoritative application identity. Refresh must deterministically reconstruct the same record from verified repository/package metadata.
