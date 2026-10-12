<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Release contract

Software has one permanent verification and publication path: `.github/workflows/ci.yml`.

`VERSION` is the release identity. `README.md`, the first Debian changelog stanza and the newest AppStream release entry must agree with it. Commit-message wording is not part of release detection.

Every pull request verifies the source. Every push to `main` verifies the exact pushed commit. The Debian package is built once by the verification job, checked by the package contract and smoke tests, then carried forward as the same immutable artifact for publication. Release jobs do not rebuild it.

A push to `main` is release-eligible, and a manual recovery run may target only `main` or the exact matching `vVERSION` tag. An exact rerun of a commit already carrying that tag is eligible, which makes recovery deterministic. A reused version whose tag points at another commit is a hard failure rather than a silently skipped release. If a release exists with an incomplete asset set, the verified bundle is re-uploaded with replacement semantics.

GitHub publication must complete before central APT publication. The APT step requires the catalogue to advertise the exact Software version, Debian package identity and expected release asset before publication is considered complete; it dispatches repository publication when necessary, then waits for that verified result. This makes reruns safe when an earlier attempt stopped between GitHub publication and repository publication.

`main` is the only persistent development branch. Temporary branches used for exceptional recovery must be removed when the operation completes.
