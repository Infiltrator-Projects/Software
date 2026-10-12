<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Release contract

Software has one permanent verification and publication path: `.github/workflows/ci.yml`.

`VERSION` is the release identity. `README.md`, the first Debian changelog stanza and the newest AppStream release entry must agree with it. Commit-message wording is not part of release detection.

Every pull request verifies the source. Every push to `main` verifies the exact pushed commit. The Debian package is built once by the verification job, checked by the package contract and smoke tests, then carried forward as the same immutable artifact for publication. Release jobs do not rebuild it.

A `main` commit is release-eligible when its `VERSION` has no existing `vVERSION` tag. An exact rerun of a commit already carrying that tag is also eligible, which makes recovery deterministic. If a release exists with an incomplete asset set, the verified bundle is re-uploaded with replacement semantics; an existing tag that points at any other commit is a hard failure.

GitHub publication must complete before central APT publication. The APT step first checks whether the catalogue already advertises the exact version, dispatches repository publication when necessary, then waits for and verifies the catalogue result. This makes reruns safe when an earlier attempt stopped between GitHub publication and repository publication.

`main` is the only persistent development branch. Temporary branches used for exceptional recovery must be removed when the operation completes.
