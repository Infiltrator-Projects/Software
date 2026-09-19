<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Transaction contract

Package writes are intentionally disabled in the initial milestone.

A mutating operation progresses through explicit phases: Refresh, Resolve, Present, Authorize, Stage, Checkpoint, Execute, Verify, Record and Recover.

The backend resolves the complete change set before authorization. The UI presents installs, upgrades, removals, download bytes, disk delta and system-critical impact.

The privileged executor never interprets arbitrary human shell commands. It receives a typed resolved plan tied to known package/repository state. If that state changes materially before execution, the operation is resolved again.

Transactions touching kernels, the package engine, init/system services, libc or another backend-classified critical component are visibly distinguished. This is a higher-risk transaction class inside the same application, not a second updater program.

When InfiltratorFS is available, qualifying system transactions may request a pre-change filesystem checkpoint. Package history stores the checkpoint identity, but filesystem rollback remains owned by a recovery service rather than by the package backend itself.
