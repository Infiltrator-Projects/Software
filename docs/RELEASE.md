<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Release contract

Software has one permanent verification and GitHub publication entry point: `.github/workflows/ci.yml`. The workflow is orchestration only; deterministic release policy lives in `tools/release.sh`, where the same identity, bundle and GitHub publication checks can be invoked outside GitHub Actions instead of being duplicated as large inline shell programs in YAML.

`tools/release.sh` anchors itself to the repository root before evaluating source, git or bundle paths. Callers therefore do not need to reproduce the checkout working directory, and the source-bundle and complete-bundle commands accept only the inputs they actually require.

`VERSION` is the release identity. `README.md`, the first Debian changelog stanza and the newest AppStream release entry must agree with it. `tests/source_contracts.py` enforces that metadata contract. Commit-message wording is not part of release detection.

Every pull request verifies the source. Every push to `main` verifies the exact pushed commit. Verification concurrency is keyed by Git ref, so a newer push to the same branch cancels a superseded run before that older state can race the current state to publication; unrelated refs remain independent. The Debian package is built once, checked by the package contract and smoke tests, and remains the exact package carried into the completed release bundle. Release publication never rebuilds it.
Verification-only runs do not fetch remote release tags; tag state is fetched only after a run is determined to be publication/recovery eligible, so ordinary source verification does not depend on an unnecessary extra Git network operation.

A push to `main` is release-eligible, and a manual recovery run may target only `main` or the exact matching `vVERSION` tag. An exact rerun of a commit already carrying that tag is eligible, which makes recovery deterministic. A reused version whose tag points at another commit is a hard failure rather than a silently skipped release. Release assets are byte-immutable once published: recovery validates every existing asset against the verified local SHA-256 digest and uploads only assets that are genuinely missing. An existing asset with the right name but different bytes is a hard failure requiring a new version; recovery never clobbers published bytes under an existing version identity.

GitHub publication is the final step of the same job that qualified the release bundle. This is deliberate: the exact on-disk bundle that passed source, unit/integration, package, D-Bus, graphical and AppStream verification is passed directly to `tools/release.sh publish-github`. There is no Actions-artifact upload/download relay, second checkout or cross-job release state to expire, fill a storage quota or diverge from qualification. Checkout does not persist credentials, and the GitHub token is exposed only to the final publication step. `publish-github` still verifies the exact checkout, tag identity, checksum manifest, Debian package identity/version and complete release-bundle file set before it creates or repairs the release.

Central APT publication is deliberately owned by `Infiltrator-Projects/Infiltrator-Repository`, which consumes immutable application releases and builds/signs/deploys the repository as its own lifecycle. Software CI does not require a cross-repository dispatch secret, does not poll GitHub Pages, and does not fail because an independent repository workflow or scheduled run is delayed. This keeps application release correctness separate from repository aggregation while allowing central publication to be retried independently without rebuilding or retagging Software.

The verification job intentionally uses the declared GitHub-hosted Ubuntu runner. The former self-hosted dependency branch was unreachable under that runner declaration and has been removed rather than retained as dead release logic.

`main` is the only persistent development branch. Temporary branches used for exceptional recovery must be removed when the operation completes.
