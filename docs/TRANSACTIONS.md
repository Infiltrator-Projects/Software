<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Transaction contract

Package mutation is a first-class Software capability. The contract is backend-neutral. Current Software uses the native Debian-compatibility engine for state, policy, dependency resolution and preflight planning while a constrained APT/dpkg bridge remains only at the privileged payload-execution boundary.

## Phases

A mutating operation progresses through explicit phases:

    Refresh → Resolve → Present → Authorize → Stage
            → Checkpoint → Execute → Verify → Record → Recover

No phase may be silently skipped merely because a backend can perform the entire operation in one command.

## Resolve before authorization

The engine resolves the complete intended change set before asking for privilege.

The plan contains exact package identities and versions, installs, upgrades, dependency-driven changes, download bytes, estimated disk delta, repository/source provenance, payload filename and SHA-256, system-critical classification and the state generation used for the calculation.

The native planner produces install, upgrade and removal plans from a coherent state generation. It resolves dependencies before constructing the immutable plan, distinguishes explicit requests from dependency-induced changes, enforces hold and downgrade policy, rejects unresolved conflicts and incompatible final dependency requirements, and computes aggregate download/disk impact. Removal planning uses retained installed dependency/provides/Essential metadata, rejects Essential removal and fails closed when a retained package would lose a hard dependency.

The GUI presents the meaningful consequences before authorization.

## Immutable execution request

The privileged executor receives a typed resolved plan, not an arbitrary command line.

During the current compatibility bridge, the GUI serializes the resolved plan as exact `package=version` specifications, with approved removals explicitly marked, for the constrained helper. Discover installs/removals and Updates use the same reviewed plan contract. After administrator authorization the helper refreshes root-owned repository metadata, then simulates the exact intended APT operation and compares every resulting mutation against the approved package identity, architecture, action and version. Execution proceeds only when the simulated transaction matches the approved plan one-for-one; an added dependency, missing approved change, unapproved removal, architecture drift or stale/no-op plan aborts before mutation. Implicit Recommends/Suggests are disabled. The legacy `apply` entry point remains upgrade-only for older clients.

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

The current opaque privileged compatibility subprocess cannot provide trustworthy per-package completion events. Software therefore keeps an indeterminate graphical progress indicator and elapsed-time status visible from authorization through metadata refresh, exact-plan revalidation, package application and final state refresh.

Structured phase/package/completed-total/cancellability events remain the contract for the future native payload executor; the documentation does not present them as implemented today.

## Verification and history

Before mutation, the privileged compatibility helper re-simulates the exact approved plan against refreshed metadata and refuses any mutation-set drift. After successful execution, the native engine refreshes authoritative installed/repository state and publishes a new generation before Installed and Discover reload. The current bridge verifies coherent final package state and remaining update candidates; item-by-item native postcondition verification belongs to the future native payload executor.

History records transaction identity, start/end time, requested operation, resolved before/after versions, source provenance, outcome and failure detail. Recovery/checkpoint linkage is reserved for the recovery milestone.

## Recovery

Repair is operational for state-preserving recovery. It can reconcile authoritative installed state against the current verified repository generation, rebuild verified repository state on request, diagnose dpkg audit/update-fragment problems, and authenticate the narrow `dpkg --configure -a` action for packages that are already unpacked but unfinished.

Repair does not run an unconstrained `apt --fix-broken install`. Any future dependency-changing repair must first resolve and display an immutable package mutation plan under the same authorization rules as normal transactions.

Checkpoint creation and guided rollback remain the full 0.6 recovery milestone. Qualifying InfiltratorFS transactions may then request a pre-change filesystem checkpoint, with checkpoint identity linked to History and recovery.
