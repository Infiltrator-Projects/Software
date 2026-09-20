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
