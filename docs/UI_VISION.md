# Software UI Vision

## Purpose

This document records the visual north star for the Software redesign.

![Software visual target](design/software-ui-target.jpg)

The target image is a design prototype, not a literal widget specification. It captures the visual density, hierarchy, colour, confidence and GUI-first direction the implementation should move toward while preserving native GTK behaviour, accessibility and the existing package-management architecture.

## Core intent

Software should feel:

- graphical;
- premium;
- colourful;
- modern;
- GUI-first;
- welcoming; and
- visually rich.

It should not feel:

- text-heavy;
- austere;
- purely administrative;
- CLI-minded; or
- like an early-alpha control utility.

## Influence

The intended spirit is closer to:

- Amiga / Workbench-style visual-computing thinking;
- a desktop application designed for direct graphical interaction;
- strong application identity rather than generic settings-window presentation; and
- modern rich software-centre interfaces without abandoning native desktop behaviour.

This is influence, not imitation. Software should remain recognisably Infiltrator.

## Design principles

- Use strong visual hierarchy.
- Prefer visual cards and panels to long flat text-heavy surfaces.
- Show icons, state, provenance and actions at a glance.
- Prefer graphical previews and meaningful visual status where practical.
- Keep category navigation clear and persistent.
- Make Discover feel like browsing software, not reading a package table.
- Make Updates feel like a controlled visual workflow rather than an APT front end.
- Make History read as a chronological activity surface.
- Make Repair read as a recovery centre with health/state first and technical detail available second.
- Preserve native usability, keyboard access and accessibility.
- Preserve engineering practicality without allowing implementation convenience to dictate an austere UI.
- Keep the current Common typography/theme contract, but use it with richer composition, spacing and semantic colour.

## First implementation direction

The first pass should move the existing GTK UI toward this target without discarding the working engine:

1. strengthen the application shell and navigation hierarchy;
2. turn page introductions into richer visual hero panels;
3. make summary/status cards more graphical and dimensional;
4. make Discover cards visually dominant rather than metadata dominant;
5. convert Updates controls into a clear primary workflow with less administrative wording;
6. give History stronger timeline/activity styling; and
7. give Repair a visually distinct health-and-recovery presentation.

## Convergence progress

The implementation now has the branded navigation shell, graphical page heroes, richer application cards, category shortcuts, grouped update cards, a stateful Repair health centre and a dynamic Discover spotlight driven by the actual filtered catalogue.

The next convergence layer moves identity and search into the application chrome, removes duplicate branding from the sidebar, adds a Welcome surface plus direct graphical dashboard cards for Updates, System Health and Repositories, and tightens contrast where the live implementation remained visually flatter than the target. This makes the top half of Discover read as a product dashboard rather than a decorated catalogue.

The fifth convergence layer concentrates on the sidebar itself: icon shapes and colours now follow the target more closely, the selected item becomes a vivid blue-violet surface, the administrative NAVIGATE label and chevrons are removed, Updates gains a real red count badge, and Settings becomes a proper bottom preferences card instead of technical backend/version text.

Behavioural package-management code is not to be rewritten merely to achieve the visual redesign. The UI can evolve iteratively over the existing tested engine.
