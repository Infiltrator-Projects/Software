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


### Sixth convergence layer

Discover now follows the approved target composition much more directly. The single large spotlight/administrative glance emphasis has been replaced in the visible dashboard by a Featured Applications row, a real Updates preview, Repository Status and Recent Activity side panels, and a graphical system-health banner linking directly to Repair. The full searchable catalogue remains below the dashboard rather than competing with it for first-screen attention.


### Window and scrolling correction

The visual redesign must remain a usable desktop application at every intermediate stage. The main window now owns explicit minimise, maximise/restore and close controls rather than relying on compositor-generated title buttons, remains explicitly resizable, and Discover has a non-overlay page scrollbar so the growing dashboard and catalogue remain reachable at ordinary and maximised window sizes.

### Seventh convergence layer — superseded

The initial attempt to make the Welcome surface more graphical used native Cairo-drawn shapes. That preserved live GTK text, but it was still a procedural approximation and did not reproduce the authored raster character of the approved prototype. This approach is retained here only as design history and is no longer the implementation target.

### Eighth convergence layer — raster artwork

The procedural Welcome illustration has been removed. Discover now uses a real raster hero surface derived from the approved visual reference and displayed through GtkPicture with cover scaling. The composed sky, mountains, planet, checker sphere, retro computer and Infiltrator colour ribbons are therefore authored pixels rather than programmatically reconstructed primitives.

The first raster implementation still depended on an installed filesystem path and could silently degrade to an empty hero panel even when the package build itself was green. That deployment failure mode is no longer acceptable. The hero raster is now embedded directly in the Software executable, decoded from compiled image bytes at runtime and turned into a GdkTexture before GtkPicture receives it. The visible artwork therefore travels with the executable rather than depending on a second file appearing at a particular path.

CI reconstructs and validates the complete embedded JPEG, checks its start/end markers and minimum decoded size, and the graphical smoke test fails if GTK cannot decode it. Release packaging additionally verifies that the JPEG payload is present inside the installed executable.

The implementation rule is explicit: layout, interaction and dynamic information remain native GTK, while high-visibility decorative artwork may use authored raster surfaces when the approved design depends on illustration, texture or compositing that should not be recreated as symbolic widgets or Cairo geometry.

