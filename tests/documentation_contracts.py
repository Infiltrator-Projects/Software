#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Contracts for maintained architecture documentation and repository policy.

These checks protect statements that materially define ownership and failure
semantics. They deliberately avoid requiring preferred prose: source tests own
implementation boundaries; this file rejects maintained documentation or
repository structure that creates a second authority, stale version truth, or
an incomplete maintained-document index.
"""

from pathlib import Path
import re


def text(path: str) -> str:
    return Path(path).read_text()


document_index = text("docs/README.md")
readme = text("README.md")
maintained = {
    "architecture": text("docs/ARCHITECTURE.md"),
    "package_engine": text("docs/PACKAGE_ENGINE.md"),
    "performance": text("docs/PERFORMANCE.md"),
    "roadmap": text("docs/ROADMAP.md"),
    "state": text("docs/STATE.md"),
}

# docs/README.md calls itself the maintained-document entry point. Make that
# claim executable: every maintained Markdown contract beside the index itself
# must be discoverable from it. This protects navigation/ownership truth while
# remaining indifferent to ordering, labels and prose style.
for path in sorted(Path("docs").glob("*.md")):
    if path.name == "README.md":
        continue
    assert f"({path.name})" in document_index, (
        f"docs/README.md does not index maintained document {path.name}"
    )

# Release documentation defines .github/workflows/ci.yml as the one permanent
# verification/publication authority. Temporary repair or publication workflows
# are release state, not harmless clutter: leaving one behind creates another
# executable path around the qualified-artifact contract.
workflows = {
    path.name
    for path in Path(".github/workflows").iterdir()
    if path.is_file()
}
assert workflows == {"ci.yml"}, (
    f"unexpected permanent workflow set: {sorted(workflows)}"
)

# Common is pinned by the gitlink and Common's own VERSION file. README may
# describe that contract, but must not copy the current Common version into a
# second manually maintained source of truth.
assert re.search(r"\bCommon\s+[0-9]+\.[0-9]+\.[0-9]+\b", readme) is None, (
    "README duplicates the authoritative Common version"
)

# Installed inventory is engine-owned. Historical migration prose may correctly
# say that the engine reads dpkg status, and current prose may explicitly deny a
# fallback. Reject positive fallback claims and wording that makes the engine
# merely a preferred/optional Installed authority.
installed_fallback = re.compile(
    r"\bInstalled\b.*\b(?:uses|retains|falls\s+back)\b.*\bfallback\b",
    re.IGNORECASE,
)
installed_optional_engine = re.compile(
    r"\bInstalled\b.*\bprefer(?:s|red)?\b.*\b(?:engine|shared\s+state)\b",
    re.IGNORECASE,
)
for name, document in maintained.items():
    assert "compatibility fallback" not in document.lower(), (
        f"{name} reintroduces the retired compatibility fallback"
    )
    for line in document.splitlines():
        if installed_fallback.search(line):
            raise AssertionError(f"{name} reintroduces an Installed fallback: {line}")
        if installed_optional_engine.search(line):
            raise AssertionError(
                f"{name} makes the Installed engine authority optional: {line}"
            )

# The package-state implementation is the sole authority for the current schema
# number. Documentation describes persisted fields and migration invariants so
# a normal schema increment cannot make maintained design prose factually stale.
assert re.search(
    r"schema version\s+[0-9]+",
    maintained["state"],
    re.IGNORECASE,
) is None
