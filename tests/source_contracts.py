#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Durable architecture, authorization and release contracts for Software.

This test protects repository properties that remain meaningful when internals
are refactored: dependency direction, authority boundaries, privilege
separation, release identity and externally visible packaging/protocol
contracts. Algorithmic behaviour belongs in compiled tests.

A test in this file should fail because an architectural contract changed, not
because a helper was renamed or an equivalent expression was rewritten.
"""

from pathlib import Path
import re
import xml.etree.ElementTree as ET


def text(path: str) -> str:
    return Path(path).read_text()


def local_includes(source: str) -> set[str]:
    return set(
        re.findall(
            r'^\s*#\s*include\s+"([^"]+)"\s*$',
            source,
            re.MULTILINE,
        )
    )


def cmake_target_body(cmake: str, command: str, target: str) -> str:
    pattern = re.compile(
        rf"\b{re.escape(command)}\s*\(\s*{re.escape(target)}\b(.*?)\)",
        re.DOTALL,
    )
    bodies = [match.group(1) for match in pattern.finditer(cmake)]
    assert bodies, f"missing {command} declaration for {target}"
    return "\n".join(bodies)


def cmake_target_tokens(cmake: str, command: str, target: str) -> set[str]:
    return set(
        re.findall(
            r"[A-Za-z0-9_:+./${}-]+",
            cmake_target_body(cmake, command, target),
        )
    )


cmake = text("CMakeLists.txt")
workflow = text(".github/workflows/ci.yml")
version = text("VERSION").strip()
changelog = text("debian/changelog")
control = text("debian/control")

app_sources = {
    path: path.read_text()
    for path in Path("src/app").glob("*.[ch]pp")
}
main_cpp = text("src/app/main.cpp")
app_coordinator = text("src/app/app_coordinator.cpp")
process_support = text("src/app/process_support.cpp")

source_configuration = text("src/engine/debian_source_configuration.cpp")
installed_state = text("src/engine/debian_installed_state.cpp")
package_index = text("src/engine/debian_package_index.cpp")
reconcile = text("src/engine/debian_reconcile.cpp")

engine_service = text("src/service/main.cpp")
engine_interface_path = Path("data/net.ssmith.infiltrator.software.Engine.xml")
engine_interface = engine_interface_path.read_text()

metainfo_root = ET.parse(
    "debian/infiltrator-software.metainfo.xml"
).getroot()
metainfo_releases = metainfo_root.findall("./releases/release")

update_policy_path = Path(
    "data/net.ssmith.infiltrator.software.updates.policy"
)
source_policy_path = Path(
    "data/net.ssmith.infiltrator.software.policy"
)

# Release identity is externally visible state. Keep one semantic version across
# package metadata without coupling the test to README wording.
assert re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version)
assert changelog.startswith(f"infiltrator-software ({version}) ")
assert metainfo_releases
assert metainfo_releases[0].get("version") == version
for marker in ("Warning: truncated output", "tokens truncated", "Total output lines:"):
    assert marker not in changelog

# The release workflow qualifies and publishes one package in one linear path.
# Check the guarantees, not step names or YAML prose.
assert workflow.count("dpkg-buildpackage -us -uc -b") == 1
assert "actions/upload-artifact@" not in workflow
assert "actions/download-artifact@" not in workflow
assert re.search(r"(?m)^\s*cancel-in-progress:\s*true\s*$", workflow)
assert re.search(r"(?m)^\s*persist-credentials:\s*false\s*$", workflow)
build_position = workflow.index("dpkg-buildpackage -us -uc -b")
publish_match = re.search(r"\btools/release\.sh\s+publish-github\b", workflow)
assert publish_match is not None
assert build_position < publish_match.start()

# The graphical product consumes client/domain capabilities. Package-engine
# implementation and privileged source mutation are not GUI dependencies.
gui_links = cmake_target_tokens(
    cmake, "target_link_libraries", "infiltrator-software"
)
assert "software-client" in gui_links
assert "software-source-inventory" in gui_links
assert "software-engine" not in gui_links
assert "software-source-mutation" not in gui_links

for path, source in app_sources.items():
    for include in local_includes(source):
        assert not include.startswith("engine/"), (
            f"{path} bypasses client/domain boundaries with {include}"
        )

# The executable shell/coordinator know navigation abstractions, not concrete
# page implementations. Adding or renaming a page does not require this test to
# enumerate it.
for owner_name, owner_source in (
    ("src/app/main.cpp", main_cpp),
    ("src/app/app_coordinator.cpp", app_coordinator),
):
    page_headers = [
        include
        for include in local_includes(owner_source)
        if include.startswith("app/") and include.endswith("_view.hpp")
    ]
    assert not page_headers, (
        f"{owner_name} directly depends on page views: {page_headers}"
    )

# GUI process creation has one low-level owner. Callers may use any higher-level
# helper API without this test depending on that helper's spelling.
low_level_process_apis = (
    "g_subprocess_new(",
    "g_subprocess_newv(",
    "g_subprocess_launcher_new(",
    "g_spawn_",
)
assert any(api in process_support for api in low_level_process_apis)
for path, source in app_sources.items():
    if path.name == "process_support.cpp":
        continue
    for api in low_level_process_apis:
        assert api not in source, (
            f"{path} bypasses the GUI process boundary with {api}"
        )

# Debian source syntax has one shared parser capability. Inventory and engine
# reconciliation can read it; only the privileged helper receives mutation.
engine_links = cmake_target_tokens(
    cmake, "target_link_libraries", "software-engine"
)
inventory_links = cmake_target_tokens(
    cmake, "target_link_libraries", "software-source-inventory"
)
mutation_links = cmake_target_tokens(
    cmake, "target_link_libraries", "software-source-mutation"
)
source_helper_links = cmake_target_tokens(
    cmake, "target_link_libraries", "infiltrator-software-helper"
)
assert "software-source-format" in engine_links
assert "software-source-format" in inventory_links
assert "software-source-format" in mutation_links
assert "software-source-mutation" in source_helper_links
assert "sources/apt_source_format.hpp" in local_includes(source_configuration)

# Debian control paragraphs likewise have one engine parser authority. These
# consumers depend on the shared parser interface; compiled tests own syntax
# and field behaviour.
assert "engine/debian_control.hpp" in local_includes(installed_state)
assert "engine/debian_control.hpp" in local_includes(package_index)

# Native reconciliation must remain process-free. This is an ownership boundary,
# independent of helper names or the exact way state files are parsed.
for process_api in (
    "g_spawn_",
    "g_subprocess_",
    "std::system(",
    "popen(",
    "fork(",
    "posix_spawn",
    "execv",
):
    assert process_api not in reconcile

# The installed D-Bus XML is the sole protocol schema. CMake embeds that file
# into the service; the service must not carry another raw XML document.
assert engine_interface_path.is_file()
interface_root = ET.fromstring(engine_interface)
interfaces = interface_root.findall("interface")
assert len(interfaces) == 1
assert interfaces[0].get("name") == "net.ssmith.infiltrator.software.Engine"
public_methods = {
    method.get("name")
    for method in interfaces[0].findall("method")
}
assert "PlanMixedTransaction" in public_methods
assert "Quit" in public_methods

cmake_service = cmake_target_tokens(
    cmake, "add_executable", "infiltrator-software-engine"
)
assert "src/service/main.cpp" in cmake_service
assert "engine_introspection.hpp" in engine_service
assert 'R"XML(' not in engine_service
assert (
    "data/net.ssmith.infiltrator.software.Engine.xml" in cmake
    and "src/service/engine_introspection.hpp.in" in cmake
)

# Privileged source mutation remains a narrow, administrator-authorized
# capability. The GUI cannot acquire it through its link graph.
for policy_path in (update_policy_path, source_policy_path):
    defaults = ET.parse(policy_path).getroot().find(".//defaults")
    assert defaults is not None
    assert defaults.findtext("allow_any") == "auth_admin"
    assert defaults.findtext("allow_inactive") == "auth_admin"
    assert defaults.findtext("allow_active") == "auth_admin"

# System preferences remain one literal authority in product code. Consumers
# refer to the shared definition rather than copying the path.
product_sources = "\n".join(
    path.read_text()
    for path in Path("src").rglob("*.[ch]pp")
    if "src/infiltratr-common/" not in path.as_posix()
)
assert product_sources.count(
    '"/etc/infiltrator-software/automatic-updates.conf"'
) == 1

# Software coexists with Linux Mint Update Manager; parity does not mean package
# replacement.
assert "Provides: mintupdate" not in control
assert "Conflicts: mintupdate" not in control
assert "Replaces: mintupdate" not in control

# Common is consumed as the shared foundation rather than copied into the
# Software payload. CMake itself verifies the checked-out submodule against the
# superproject gitlink.
assert "InfiltratrCommon::Common" in gui_links
assert 'rev-parse "HEAD:src/infiltratr-common"' in cmake
assert 'add_subdirectory("${INFILTRATR_COMMON_DIR}"' in cmake
