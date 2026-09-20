<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Architecture

Infiltrator Software is one user-facing application with deliberately separated internal responsibilities. The historic separation between a software catalogue and an update manager is not reproduced as a product boundary.

## Layers

The native shell owns navigation, interaction, accessibility, platform appearance detection and rendering. It never invokes APT, dpkg or another package engine directly.

The core owns package/application identity, classification, channels, installed/available state, transaction requests and transaction plans. These types are package-manager-neutral.

A backend translates one native package ecosystem into the core model. Capabilities are explicit so the UI never assumes that a backend supports mutation merely because it can inventory packages.

APT/.deb is the first package backend. Installed-package inventory remains read-only. Discover is deliberately separate from package mechanics: one catalogue source reads authoritative Infiltrator application records, while a system catalogue source consumes the host AppStream pool including configured Flatpak metadata. These records are merged by application/package identity and reconciled with local APT and Flatpak installed state. Update calculation and transaction planning remain required before package mutation.

Common 1.19.10 owns reusable project-family facilities such as semantic theme design and other product-neutral mechanisms. Software's appearance controller consumes Common's System/Day/Night mode policy, semantic palettes, typography roles and structural metrics. Follow OS listens for GTK desktop-theme changes and resolves System dynamically. The selected mode is stored atomically through Common's POSIX durability API. Package semantics remain local to Software.

## Dependency rule

```text
native shell
    |                 |
    v                 v
product core      catalogue sources <---- Infiltrator Repository
    |                    ^
    |                    +---- host AppStream / Flatpak metadata
    v
backend contract <---- APT implementation
    |
    +---- source inventory <---- /etc/apt + Flatpak remotes
    |
    +---- Common product-neutral facilities
```

No dependency points from the core into APT.

## Privilege model

The graphical process never runs as root. Catalogue loading, installed inventory, APT source inventory and Flatpak user-remote management remain unprivileged.

System APT source addition is the first privileged operation. It is isolated in `/usr/libexec/infiltrator-software-helper`, authorized by a dedicated Polkit action, accepts only a fixed `add-apt-source` operation, validates every argument, requires HTTPS, rejects newline/whitespace injection, restricts optional `Signed-By` paths to standard keyring directories and writes a single modern Deb822 `.sources` file atomically. The helper does not accept arbitrary commands or shell text.

Future package mutation remains a separate design: unprivileged planning, explicit authorization, a minimal transaction executor, then unprivileged verification/history presentation.

## Concurrency

Repository/network operations must not block the GTK main loop. Discover performs Infiltrator HTTPS catalogue refresh, host AppStream/Flatpak catalogue loading and installed-state reconciliation off the GTK thread. Catalogue results publish first; verified first-party icon hydration remains a separate generation-checked background task so slow icons cannot withhold the catalogue UI. Live first-party HTTPS metadata is atomically cached; a network failure can fall back to the last valid cached document without converting cache data into an authoritative source.

## Failure model

Backend failures are data, not crashes. Every operation returns either a typed result or an explicit error. Partial state must not be silently represented as authoritative complete state.

The verification pipeline includes a real GTK launch smoke test under Xvfb. It must keep the application alive through the asynchronous Discover catalogue callback window; an unexpected exit, abort or segmentation fault fails CI. The test captures stdout/stderr and obtains a debugger backtrace on failure.
