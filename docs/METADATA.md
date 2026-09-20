<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Authoritative software metadata

Traditional Linux software presentation can derive identity from Debian control fields, desktop files, AppStream, repository catalogues, icon themes and local caches. When those disagree, the user can see missing icons, duplicate names, stale descriptions or a catalogue entry that does not match the installed application.

Infiltrator Software treats that as an identity-integrity problem.

## Canonical record

The normalized identity contains, where applicable, a stable application ID, package identity, display name and publisher, installed and available versions, summary and description, icon identity and digest, screenshots, desktop integration, repository/source, release channel, architecture, software classification, download and installed sizes, dependency/provides relationships, trust/signature state and system-critical state.

The canonical record is independent of the mechanism used to obtain the data.

## Source authority

Repository/package metadata remains authoritative. Software's local database is derived state for speed, indexing and coherent presentation.

Deleting the local derived database must not destroy package identity. A refresh must reconstruct the same normalized records from authoritative sources.

APT private cache files are not authoritative inputs.

## First-party repository contract

For first-party Infiltrator applications, repository metadata is generated from the same immutable release identity that publishes the package.

The catalogue icon and desktop icon installed by the package originate from the same release package. Published application records carry an icon URL and SHA-256 digest. Software downloads only over trusted transport, verifies the digest and caches by content identity.

Changing an icon creates new release metadata and a new digest.

## Third-party Debian metadata

The native Debian-compatibility engine normalizes supported Debian repository fields directly rather than asking an APT executable to interpret them for the GUI.

Package mechanics and application presentation remain distinct. A package can exist without being presented as a graphical application. Transitional, dependency-only and virtual packages are not promoted to duplicate application entries.

## AppStream

AppStream enriches package mechanics with application-facing identity such as human names, summaries, screenshots, categories and launch integration.

Conflicts between AppStream, repository metadata and installed desktop integration are retained with provenance rather than silently merged into an invented identity.

## Flatpak

Flatpak identities are normalized into the same application-level model but retain Flatpak provenance, scope, branch/runtime information and remote identity.

Flatpak state is never represented as a Debian package simply to simplify the UI.

## Cache rule

Caches are disposable derived data.

A cache may improve startup, search and rendering, but must never become the only copy of authoritative identity, hide source provenance, overwrite newer state with stale asynchronous work, or require network access to render previously known applications.

See [State](STATE.md).
