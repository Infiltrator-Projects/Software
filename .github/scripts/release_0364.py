from pathlib import Path

version = Path("VERSION")
assert version.read_text().strip() == "0.3.63"
version.write_text("0.3.64\n")

readme = Path("README.md")
text = readme.read_text()
old = "**Current source version:** 0.3.63<br>"
assert old in text
text = text.replace(old, "**Current source version:** 0.3.64<br>", 1)
assert "**Shared foundation:** Common 1.19.38<br>" in text
readme.write_text(text)

changelog = Path("debian/changelog")
text = changelog.read_text()
entry = """infiltrator-software (0.3.64) unstable; urgency=high

  * Replace the resident tray's two-second filesystem/process polling with
    event-driven GFileMonitor watches for preferences, runtime state and
    installed executable replacement.
  * Align Software shell radii and title semantics with current
    InfiltratorOS/System Settings by consuming Common design metrics for
    search, window controls, navigation wells and card surfaces.
  * Strengthen source-contract guards against reintroducing rapid tray
    polling or hard-coded shell geometry.
  * Re-verify the exact suite-current Common 1.19.38 pin at
    7070c5812b50821fd7580101cb2289a3184f6b2c.

 -- Shannon Smith <infiltratr@yandex.com>  Sat, 03 Oct 2026 14:42:00 +1000

"""
assert text.startswith("infiltrator-software (0.3.63)")
changelog.write_text(entry + text)

metainfo = Path("debian/infiltrator-software.metainfo.xml")
text = metainfo.read_text()
anchor = "  <releases>\n"
assert anchor in text
release = """    <release version=\"0.3.64\" date=\"2026-10-03\">
      <description>
        <p>Removes the remaining two-second tray poll, switches runtime changes to event-driven monitors, and aligns shell geometry with current InfiltratorOS/Common metrics.</p>
      </description>
    </release>
"""
text = text.replace(anchor, anchor + release, 1)
assert text.count('release version="0.3.64"') == 1
metainfo.write_text(text)
