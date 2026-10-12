<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Release contract

Software has one permanent verification and publication entry point: `.github/workflows/ci.yml`. The workflow is orchestration only; deterministic release policy lives in `tools/release.sh`, where the same identity, bundle and publication checks can be invoked outside GitHub Actions instead of being duplicated as large inline shell programs in YAML.

`VERSION` is the release identity. `README.md`, the first Debian changelog stanza and the newest AppStream release entry must agree with it. `tests/source_contracts.py` enforces that metadata contract. Commit-message wording is not part of release detection.

Every pull request verifies the source. Every push to `main` verifies the exact pushed commit. The Debian package is built once by the verification job, checked by the package contract and smoke tests, then carried forward as the same immutable artifact for publication. Release jobs do not rebuild it.

A push to `main` is release-eligible, and a manual recovery run may target only `main` or the exact matching `vVERSION` tag. An exact rerun of a commit already carrying that tag is eligible, which makes recovery deterministic. A reused version whose tag points at another commit is a hard failure rather than a silently skipped release. If a release exists with an incomplete asset set, the verified bundle is re-uploaded with replacement semantics.

GitHub publication verifies the exact checkout, tag identity, checksum manifest, Debian package identity/version and complete release-bundle file set before it creates or repairs the release. Third-party artifact transfer actions are commit-pinned so a moving action tag cannot silently change the release path.

GitHub publication must complete before central APT publication. The APT step requires the catalogue to advertise the exact Software version, Debian package identity and expected release asset before publication is considered complete; it dispatches repository publication when necessary, then waits for that verified result. This makes reruns safe when an earlier attempt stopped between GitHub publication and repository publication.

The verification job intentionally uses the declared GitHub-hosted Ubuntu runner. The former self-hosted dependency branch was unreachable under that runner declaration and has been removed rather than retained as dead release logic.

`main` is the only persistent development branch. Temporary branches used for exceptional recovery must be removed when the operation completes.
