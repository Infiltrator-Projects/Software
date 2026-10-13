#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Contracts for maintained architecture documentation.

These checks protect statements that materially define ownership and failure
semantics. They deliberately avoid requiring preferred prose: source tests own
implementation boundaries; this file only rejects maintained documentation that
creates a second package-state authority, a duplicate schema-version truth, or
an incomplete maintained-document index.
"""

from pathlib import Path
import re


def text(path: str) -> str:
    return Path(path).read_text()


document_index = text("docs/README.md")
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
