#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Contracts for maintained architecture documentation.

These checks protect statements that materially define ownership and failure
semantics. They deliberately avoid prose formatting and implementation spelling:
source tests own implementation boundaries; this file prevents maintained docs
from reintroducing a second package-state authority or a duplicate schema-version
source of truth.
"""

from pathlib import Path
import re


def text(path: str) -> str:
    return Path(path).read_text()


architecture = text("docs/ARCHITECTURE.md")
package_engine = text("docs/PACKAGE_ENGINE.md")
performance = text("docs/PERFORMANCE.md")
state = text("docs/STATE.md")

# Installed inventory is engine-owned. Maintained architecture documents must
# describe failure as an engine/client condition, never as permission for the
# GUI to create a second dpkg-status reader or reconciliation path.
assert "If the engine cannot provide Installed state" in architecture
assert "Installed consumes only shared engine state" in package_engine
assert "If the engine is unavailable, Installed reports that condition" in performance
assert "does not create a second package-state authority" in state

for document in (architecture, package_engine, performance, state):
    assert "Installed alone retains" not in document
    assert "direct `/var/lib/dpkg/status` fallback" not in document

# The package-state implementation is the sole authority for the current schema
# number. Documentation describes persisted fields and migration invariants so
# a normal schema increment cannot make maintained design prose factually stale.
assert re.search(r"schema version\s+[0-9]+", state, re.IGNORECASE) is None
