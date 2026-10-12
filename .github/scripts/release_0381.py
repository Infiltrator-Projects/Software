#!/usr/bin/env python3
from pathlib import Path
import os

OLD = "0.3.80"
NEW = "0.3.81"


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one occurrence of {old!r}, found {count}")
    p.write_text(text.replace(old, new, 1))


version = Path("VERSION").read_text().strip()
if version != OLD:
    raise RuntimeError(f"expected VERSION {OLD}, found {version}")
Path("VERSION").write_text(NEW + "\n")

replace_once(
    "README.md",
    f"**Current source version:** {OLD}<br>",
    f"**Current source version:** {NEW}<br>",
)

changelog = Path("debian/changelog")
existing = changelog.read_text()
stamp = os.environ["DEB_CHANGELOG_DATE"]
stanza = f"""infiltrator-software ({NEW}) unstable; urgency=high

  * Reduce application-layer brittleness by separating shell coordination from
    page lifecycle policy and routing cross-page changes through typed events.
  * Move shared Discover/Updates presentation and transaction-history helpers
    behind neutral support APIs instead of duplicating or reaching across views.
  * Prevent Discover and Updates from consuming each other's view APIs and use
    public controller surfaces rather than backing page storage.
  * Strengthen source contracts so future changes cannot silently restore the
    cross-page coupling removed by this refactor.
  * Preserve build, tests, authorization/source contracts, engine smoke,
    graphical launch, AppStream validation and Debian-package verification.

 -- Shannon Smith <infiltratr@yandex.com>  {stamp}

"""
if not existing.startswith(f"infiltrator-software ({OLD}) "):
    raise RuntimeError("unexpected changelog head")
changelog.write_text(stanza + existing)

metainfo = Path("debian/infiltrator-software.metainfo.xml")
xml = metainfo.read_text()
needle = f'''  <releases>\n    <release version="{OLD}" date="2026-10-12">'''
insert = f'''  <releases>\n    <release version="{NEW}" date="2026-10-12">\n      <description>\n        <p>Reduces application-layer brittleness by separating page lifecycle policy from shell coordination, centralising cross-page invalidation through typed events, and enforcing neutral Discover/Updates boundaries in source contracts.</p>\n      </description>\n    </release>\n    <release version="{OLD}" date="2026-10-12">'''
if xml.count(needle) != 1:
    raise RuntimeError("unexpected AppStream release head")
metainfo.write_text(xml.replace(needle, insert, 1))
