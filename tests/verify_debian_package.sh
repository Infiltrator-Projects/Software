#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify the built Debian package as an installed artefact. Keep source-layout
# assertions out of this test: refactors must not break packaging verification
# when the package contract itself is unchanged.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <package.deb>" >&2
    exit 2
fi

package="$1"
version="$(tr -d '[:space:]' < VERSION)"

fail() {
    echo "::error::$*" >&2
    exit 1
}

field() {
    dpkg-deb -f "$package" "$1" 2>/dev/null || true
}

expect_field_exact() {
    local name="$1" expected="$2" actual
    actual="$(field "$name")"
    [[ "$actual" == "$expected" ]] || fail "$name is '$actual'; expected '$expected'."
}

expect_field_token() {
    local name="$1" token="$2" actual
    actual="$(field "$name")"
    grep -Eq "(^|, )${token}([, ]|$)" <<<"$actual" ||
        fail "$name does not contain '$token': $actual"
}

reject_field_token() {
    local name="$1" token="$2" actual
    actual="$(field "$name")"
    if grep -Eq "(^|, )${token}([, ]|$)" <<<"$actual"; then
        fail "$name must not contain '$token': $actual"
    fi
}

expect_file() {
    local path="$1"
    grep -Fqx "./${path}" package-files.txt || fail "Package is missing /${path}."
}

[[ -f "$package" ]] || fail "Debian package not found: $package"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "Invalid VERSION: $version"

expect_field_exact Package software
expect_field_exact Version "$version"
architecture="$(field Architecture)"
[[ "$architecture" =~ ^[A-Za-z0-9][A-Za-z0-9-]*$ ]] ||
    fail "Architecture is invalid: $architecture"
expect_field_token Depends apt
expect_field_token Depends pkexec
expect_field_token Depends gpgv
expect_field_token Depends gettext

grep -Eq 'libxapp' <<<"$(field Depends)" || fail "Depends does not contain libxapp: $(field Depends)"
reject_field_token Provides mintupdate
reject_field_token Replaces mintupdate
reject_field_token Conflicts mintupdate
expect_field_token Recommends flatpak

dpkg-deb --fsys-tarfile "$package" | tar -tf - > package-files.txt

for path in \
    usr/bin/software \
    usr/bin/software-tray \
    usr/bin/software-session-updater \
    usr/bin/software-cli \
    usr/bin/software-release-upgrade \
    usr/libexec/software-auto-updater \
    usr/libexec/software-maintenance \
    usr/libexec/software-helper \
    usr/libexec/software-update-helper \
    usr/libexec/software-engine \
    usr/lib/systemd/system/software-auto-update.service \
    usr/lib/systemd/system/software-auto-update.timer \
    usr/lib/systemd/system/software-maintenance.service \
    usr/lib/systemd/system/software-maintenance.timer \
    usr/share/dbus-1/services/net.ssmith.software.Engine.service \
    usr/share/dbus-1/interfaces/net.ssmith.software.Engine.xml \
    usr/share/polkit-1/actions/net.ssmith.software.policy \
    usr/share/polkit-1/actions/net.ssmith.software.updates.policy \
    etc/xdg/autostart/software-session-updater.desktop \
    etc/xdg/autostart/software-tray.desktop \
    usr/share/icons/hicolor/scalable/status/software-up-to-date-symbolic.svg \
    usr/share/icons/hicolor/scalable/status/software-updates-available-symbolic.svg \
    usr/share/icons/hicolor/scalable/status/software-checking-symbolic.svg \
    usr/share/icons/hicolor/scalable/status/software-installing-symbolic.svg \
    usr/share/icons/hicolor/scalable/status/software-error-symbolic.svg \
    usr/share/applications/net.ssmith.software.desktop \
    usr/share/icons/hicolor/scalable/apps/net.ssmith.software.svg \
    usr/share/icons/hicolor/scalable/apps/software.svg \
    usr/share/app-install/icons/software.svg \
    usr/share/metainfo/software.metainfo.xml \
    usr/share/fonts/truetype/software/mb_corpo_a_cond_regular.ttf \
    usr/share/fonts/truetype/software/mb_corpo_s_bold.ttf \
    usr/share/fonts/truetype/software/mb_corpo_s_regular.ttf; do
    expect_file "$path"
done

if grep -Eq '^\./usr/include/infiltratr/|^\./usr/lib/.*/libinfiltratr|/cmake/InfiltratrCommon/' package-files.txt; then
    fail "Software package leaked Common development/runtime payloads."
fi

root="$(mktemp -d)"
trap 'rm -rf "$root" package-files.txt' EXIT
dpkg-deb -x "$package" "$root"

desktop="$root/usr/share/applications/net.ssmith.software.desktop"
grep -Fqx 'StartupWMClass=net.ssmith.software' "$desktop" ||
    fail "Installed desktop file has the wrong StartupWMClass."
grep -Fqx 'Icon=net.ssmith.software' "$desktop" ||
    fail "Installed desktop file has the wrong icon name."

# Behaviour belongs to the installed artefact, not a source/build-tree shape.
# Exercising the packaged CLI catches packaging and executable regressions with
# the same check and remains valid if implementation files are reorganised.
"$root/usr/bin/software-cli" --version | grep -Fqx "$version" ||
    fail "Packaged CLI version does not match VERSION."
"$root/usr/bin/software-cli" --help | grep -Fq 'upgrade' ||
    fail "Packaged CLI help no longer exposes the upgrade command."

echo "Verified Debian package contract: $package"
