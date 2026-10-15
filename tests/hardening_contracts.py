#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Forensic regression contracts for native policy and release recovery."""

from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


apt_configuration = read("src/engine/debian_apt_configuration.cpp")
preferences = read("src/engine/debian_preferences.cpp")
phased = read("src/engine/debian_phased_updates.cpp")
release_script = read("tools/release.sh")
cmake = read("CMakeLists.txt")

for name, source in (
    ("APT configuration", apt_configuration),
    ("APT preferences", preferences),
    ("phased updates", phased),
):
    for process_api in (
        "g_spawn_",
        "g_subprocess_",
        "std::system(",
        "popen(",
        "fork(",
        "posix_spawn",
        "execv",
    ):
        assert process_api not in source, (
            f"native {name} regained process execution via {process_api}"
        )

assert "apt-config" not in preferences
assert "apt-config" not in phased
assert "src/engine/debian_apt_configuration.cpp" in cmake
assert '"Dir::Etc::Preferences"' in preferences
assert '"Dir::Etc::PreferencesParts"' in preferences
assert 'extension == ".conf"' in apt_configuration
assert 'extension == ".pref"' in preferences

# Same-version release recovery is additive only. Existing bytes must be
# verified against GitHub's asset digest and never overwritten in place.
assert '(.digest // "")' in release_script
assert "sha256sum" in release_script
assert "--clobber" not in release_script
