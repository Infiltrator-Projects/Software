<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Performance contract

Performance is an architectural property of Software, not a late UI optimization.

The 0.3 startup regression exposed the failure mode this document is intended to prevent: eagerly constructing every page while package inventory and multiple update calculations are already starting.

## First-frame rule

Opening Software must never require package resolution, repository refresh, network access, Flatpak enumeration, AppStream loading or a complete installed-package scan before the first window is presented.

The shell is presented first:

    start
      ↓
    load minimal preferences/theme
      ↓
    construct shell + selected-page skeleton
      ↓
    present window
      ↓
    read cached selected-page state
      ↓
    start asynchronous reconciliation

## Main-thread rule

The GTK main thread may perform only bounded UI work and small local configuration reads.

Forbidden on the GTK main thread are package database enumeration, repository index parsing, dependency solving, network access, Flatpak inventory, AppStream pool loading, package download, large-payload cryptographic verification and icon network hydration.

## Lazy pages

Only the initial page receives immediate hydration.

Other navigation pages are lightweight shells until first activation. Returning to a page shows the last known snapshot immediately and schedules reconciliation only when stale.

## Shared work

Software and the panel indicator consume the same engine state.

Starting Software must not create a second update calculation merely because the tray is running. Starting the tray must not independently rebuild package state already owned by the engine.

## Cache-first rendering

Known state is rendered from the local derived database immediately.

Freshness is represented separately from content. A page can display known information while showing that a background refresh is running.

Network failure never causes an otherwise usable page to block indefinitely.

## Generations and cancellation

Every asynchronous refresh is associated with a state generation.

When a newer refresh supersedes an older one, stale results are discarded. Long-running work supports cancellation where practical.

## Performance targets

These are engineering targets for representative supported hardware, not promises independent of hardware:

- warm start: first interactive window targeted within 500 ms;
- cold start: first interactive window targeted within 1 s;
- navigation to a previously loaded page: visually immediate;
- navigation to an unloaded page: skeleton immediately, useful cached data as soon as locally available;
- search/filter over local catalogue state: interactive without network dependency;
- tray state change after engine publication: event propagation rather than polling a second resolver.

CI should track startup and selected-page hydration timing so regressions are visible.

## Startup acceptance test

Automated graphical testing must verify that:

1. the window is mapped before background package reconciliation completes;
2. deliberately slowed repository/network workers do not delay the first frame;
3. a deliberately slowed package inventory does not freeze input;
4. opening Software while the tray is active does not spawn duplicate package scans;
5. a failed background refresh leaves the UI responsive and the last valid snapshot visible.

## Diagnostics

Useful local development measurements include process-start-to-window-mapped time, cached-page population time, selected-page reconciliation duration, package database generation duration, resolver duration, repository refresh duration and icon hydration duration.

No performance path requires external telemetry.


## Single-window activation and cache-first startup

Software is a single-instance GtkApplication. Repeated desktop or tray activation must present the existing window rather than construct another window, and `--updates` must be forwarded to the primary instance so the existing window switches to Updates.

Page construction is UI-only. Opening the application does not refresh every page. The first window is presented before selected-page reconciliation starts, and navigation lazily loads Discover, Installed, Updates and Repositories only when first visited. The global refresh control refreshes only the visible page.

The Discover software catalogue is persistent and bootstrap-once. The first successful Discover load builds the combined Infiltrator, system AppStream and Flatpak catalogue and saves it atomically. Later application launches read that saved catalogue without repository or Flatpak enumeration and without a network request. A user-requested refresh or a software-source change performs synchronization, calculates additions/removals against the saved catalogue, preserves verified icon cache paths for unchanged records, and atomically replaces the saved snapshot. Application package payloads are never downloaded merely because Software was opened.

## Shared-engine client cutover

The first client cutover is implemented. Installed inventory is no longer read on the GTK main thread. Installed and ordinary Updates hydration query the D-Bus engine from worker tasks, and the XApp panel indicator subscribes to StateChanged/HealthChanged so an engine publication is propagated as an event rather than starting a second resolver. The tray retains a low-frequency resilience check, but when engine state exists that check is a shared-snapshot read rather than a package calculation.

A compatibility fallback remains for machines on which no native package-state generation has yet been published. This is deliberately transitional. The next performance/architecture step is the native reconciliation publisher; after it owns source refresh, compatibility APT inventory scans can be removed and the no-duplicate-scan contract becomes unconditional.

## Discover reopen latency

Cached Discover startup is now explicitly two-phase. The saved catalogue is parsed and painted first; package-engine installed-state reconciliation and remote-icon hydration begin only after the first usable catalogue is on screen. A slow or unavailable engine can therefore no longer leave Discover at 0 applications / Loading while the user waits.

Icon hydration merges only icon state back into the live catalogue so it cannot overwrite a concurrent installed-state update. This preserves the fast-path rendering contract while keeping both enrichments asynchronous.

## Cached update metadata

The Updates page now follows the same cache-first principle as Discover without allowing stale metadata to masquerade as current state. Cached compatibility results are rendered first for responsiveness, then one unprivileged repository metadata refresh is scheduled in the background for the session. When that refresh completes, the visible candidate set is replaced with current repository state.

This means opening Updates does not block on network/package-manager work, while a newly published release cannot remain hidden indefinitely behind an older per-user APT cache. Manual refresh remains available and suppresses the duplicate automatic pass for that session.

## 0.3.7 interactive latency pass

The catalogue UI is now virtualized. Discover no longer creates a complete GTK widget tree for every application whenever the catalogue loads, installed state changes, icons arrive, the category changes or the user types into Search. A GtkGridView binds only the cards required for the visible viewport, while a lightweight string model contains the filtered record indices.

Installed inventory replacement is one GtkStringList splice rather than thousands of remove/append model notifications. Repository source discovery runs on a worker task. Read-only shared-engine inventory calls have a short fail-fast timeout because their compatibility fallbacks are local and safe; transaction planning and engine control retain longer timeouts.

These changes make UI latency proportional to the visible interface rather than the full package catalogue and prevent optional backend availability from dominating navigation time.
