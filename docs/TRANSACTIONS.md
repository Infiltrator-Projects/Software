<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Transaction contract

Package mutation is a first-class Software capability. The contract is backend-neutral even while the current 0.3 implementation uses APT internally and the 0.4 target uses the native Debian-compatibility engine.

## Phases

A mutating operation progresses through explicit phases:

    Refresh → Resolve → Present → Authorize → Stage
            → Checkpoint → Execute → Verify → Record → Recover

No phase may be silently skipped merely because a backend can perform the entire operation in one command.

## Resolve before authorization

The engine resolves the complete intended change set before asking for privilege.

The plan contains exact package identities and versions, installs, upgrades, dependency-driven changes, download bytes, estimated disk delta, repository/source provenance, payload filename and SHA-256, system-critical classification and the state generation used for the calculation.

The native 0.4 planner now produces install and upgrade plans from a coherent state generation. It resolves dependencies before constructing the immutable plan, distinguishes explicit requests from dependency-induced changes, enforces hold and downgrade policy, rejects unresolved conflicts, and computes aggregate download/disk impact. Native removal planning remains intentionally disabled until installed reverse-dependency state is represented, rather than guessing and risking an unsafe removal.

The GUI presents the meaningful consequences before authorization.

## Immutable execution request

The privileged executor receives a typed resolved plan, not an arbitrary command line.

During the current compatibility bridge, the GUI serializes every non-removal item from that resolved plan as an exact `package=version` specification for the constrained helper. Discover installs and Updates use the same path. The helper prohibits removal and disables implicit Recommends/Suggests. After administrator authorization it refreshes root-owned repository metadata, then runs the exact intended APT command in simulation mode and compares every resulting install/upgrade against the approved package identity, architecture and version. Execution proceeds only when the simulated transaction matches the approved plan one-for-one; an added dependency, missing approved change, removal, architecture drift or stale/no-op plan aborts before mutation. The legacy `apply` entry point remains upgrade-only for older clients.

Every privileged request is tied to a package-state generation. If authoritative state changes enough to invalidate the plan, execution stops and the operation is resolved again.

The executor rejects requests outside its supported transaction schema.

## Staging

Downloads occur before privileged execution where safely possible.

Every downloaded object is verified against repository metadata before it is eligible for execution.

A partially downloaded or failed staging operation does not mutate the installed system.

## System-critical transactions

Transactions touching kernels, boot integration, libc, the package engine, init/system services or another engine-classified critical component are visually distinguished.

This is a higher-risk transaction class inside the same Software application, not a second update manager.

## Execution boundary

During the Debian-compatibility phase, final .deb payload application may be delegated to dpkg by the constrained privileged executor.

The UI and package engine do not call dpkg directly.

The executor boundary is intentionally narrow so a future native installer can replace dpkg without changing the transaction contract.

## Progress

Execution emits structured progress events: phase, package, completed/total work, human status, technical detail and cancellability.

The GUI renders these events as graphical progress. Technical output is available under a Details disclosure with Copy support; users are not sent to a terminal.

## Verification and history

After execution, the unprivileged engine re-reads authoritative installed state and verifies the intended result.

History records transaction identity, start/end time, requested operation, resolved before/after versions, source provenance, outcome, failure detail and recovery/checkpoint linkage where applicable.

## Recovery

When InfiltratorFS is available, qualifying system transactions may request a pre-change filesystem checkpoint.

The package engine records checkpoint identity, but rollback is owned by a recovery service rather than package-format code.

An interrupted transaction appears in Repair with an explicit diagnosis and safe graphical recovery actions.
