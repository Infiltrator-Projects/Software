#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Contracts for maintained architecture documentation.

These checks protect statements that materially define ownership and failure
semantics. They deliberately avoid requiring preferred prose: source tests own
implementation boundaries; this file only rejects maintained documentation that
creates a second package-state authority or a duplicate schema-version truth.
"""

from pathlib import Path
import re


def text(path: str) -> str:
    return Path(path).read_text()


maintained = {
    "architecture": text("docs/ARCHITECTURE.md"),
    "package_engine": text("docs/PACKAGE_ENGINE.md"),
    "performance": text("docs/PERFORMANCE.md"),
    "state": text("docs/STATE.md"),
}

# Installed inventory is engine-owned. Historical migration prose may correctly
# say that the engine reads dpkg status, but maintained docs must never describe
# that file as a GUI/client fallback or another package-state authority.
for name, document in maintained.items():
    for line in document.splitlines():
        lowered = line.lower()
        if "installed" in lowered and "fallback" in lowered:
            raise AssertionError(f"{name} reintroduces an Installed fallback: {line}")
        if "/var/lib/dpkg/status" in line and "fallback" in lowered:
            raise AssertionError(f"{name} makes dpkg status a fallback: {line}")

assert "compatibility fallback" not in maintained["architecture"].lower()

# The package-state implementation is the sole authority for the current schema
# number. Documentation describes persisted fields and migration invariants so
# a normal schema increment cannot make maintained design prose factually stale.
assert re.search(
    r"schema version\s+[0-9]+",
    maintained["state"],
    re.IGNORECASE,
) is None
