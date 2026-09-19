<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Architecture

Infiltrator Software is one user-facing application with deliberately separated internal responsibilities. The historic separation between a software catalogue and an update manager is not reproduced as a product boundary.

## Layers

The native shell owns navigation, interaction, accessibility, platform appearance detection and rendering. It never invokes APT, dpkg or another package engine directly.

The core owns package/application identity, classification, channels, installed/available state, transaction requests and transaction plans. These types are package-manager-neutral.

A backend translates one native package ecosystem into the core model. Capabilities are explicit so the UI never assumes that a backend supports mutation merely because it can inventory packages.

APT/.deb is the first backend. Version 0.1 begins with read-only installed-package inventory. Catalogue search, update calculation and transaction planning are added before any write path.

Common 1.19.8 owns reusable project-family facilities such as semantic theme design and other product-neutral mechanisms. Package semantics remain local to Software.

## Dependency rule

```text
native shell
    |
    v
product core
    |
    v
backend contract <---- APT implementation
    |
    +---- Common product-neutral facilities
```

No dependency points from the core into APT.

## Privilege model

The graphical process is not intended to run as root. Read-only inventory remains unprivileged.

Future mutation is split into an unprivileged planning phase, an explicit user authorization step, a minimal privileged transaction executor, and unprivileged verification/history presentation.

The privileged component will accept a validated transaction description rather than arbitrary shell text.

## Concurrency

Repository/network operations must not block the GTK main loop. The 0.1 shell performs only the fast local installed-package inventory during activation; subsequent catalogue/update work moves to worker execution with cancellation and generation-based result publication.

## Failure model

Backend failures are data, not crashes. Every operation returns either a typed result or an explicit error. Partial state must not be silently represented as authoritative complete state.
