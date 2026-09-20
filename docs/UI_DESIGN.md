<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# UI design contract

Software is a graphical system-management application, not a GUI wrapper around terminal commands.

The interface must be modern, fast, calm and information-dense where the task requires it. It uses the Infiltrator/Common visual language rather than mimicking a generic package-manager window.

## Global principles

- one obvious primary action per view;
- meaningful state visible without opening dialogs;
- technical depth available progressively, not forced on every user;
- no normal workflow requires terminal instructions;
- no long modal progress dialog that hides the application;
- errors explain what failed and expose a graphical next action;
- cached state appears immediately while freshness updates in background;
- source/provenance is visible wherever it affects trust or behaviour;
- system-critical changes are visually distinct without creating a second updater application.

## Layout

The persistent shell contains title/header controls, navigation, the selected content page and compact global status only where useful.

The navigation contract remains Discover, Installed, Updates, System, Repositories, History and Repair.

Pages avoid repeated decorative summary cards when a compact header or grouped list communicates the same information more clearly.

## Discover

Discover is the most visual page.

It uses search, category filters, application artwork/icons, concise summaries, provenance/channel badges where useful, clear installed/install state and progressive details.

Catalogue and icon loading never hold the entire page blank.

## Installed

Installed is a working inventory view.

It favours compact rows, fast search/filtering and sortable information over large cards.

Important fields include application/package name, installed version, source and relevant state.

## Updates

Updates is a dense system-control surface.

The top communicates available count, system-critical count, freshness/last-check state and one primary Check/Install action.

Updates are grouped by meaningful class such as System, Applications and Libraries.

Before authorization the UI displays the resolved transaction, download size, disk effect and critical impact.

During execution the page remains visible and renders structured progress.

## System

System separates kernels, drivers and core operating-system components from ordinary applications.

Critical status and active/default kernel state must be understandable without interpreting package names.

## Repositories

Repositories behaves like a modern sources/settings surface.

Each source exposes display identity, type, enabled state, channel/suite, trust/signature state, health/freshness and priority where relevant.

Add, enable, disable, remove and channel changes are graphical workflows with validation before privilege.

## History

History is chronological and searchable.

Each transaction entry exposes summary, time, outcome and expandable before/after details.

Failed or recovered operations remain visible rather than disappearing.

## Repair

A healthy machine shows a simple healthy state.

Repair does not manufacture warning cards when nothing is wrong.

When a problem exists, it presents a diagnosis and safe graphical actions. Technical details are copyable.

## Progress and details

Long operations stay inside the main application where practical.

A standard transaction presentation shows operation title, progress, current package/phase, Details disclosure and Cancel only when safe.

Details may show resolver/executor diagnostics, package identities and source information. They are selectable/copyable. They are not instructions to open a terminal.

## Appearance

Common provides Follow OS, Day and Night modes plus typography, semantic colours and structural metrics.

Use the palette semantically:

- neutral surfaces for ordinary structure;
- project blue/cyan for informational/active state;
- gold for deliberate emphasis and selected/high-value state;
- orange for update attention;
- red for failure/destructive/high-risk state;
- green only for confirmed healthy/success state.

Colour never becomes the only carrier of meaning.

## Accessibility

Controls require accessible names and keyboard navigation.

Status is expressed with text/iconography in addition to colour.

Animations and transitions remain brief and do not delay interaction.

## Visual regression

Release CI should include screenshots of major states under Day and Night themes so spacing, clipping, contrast, icon regressions and accidental style drift can be reviewed.
