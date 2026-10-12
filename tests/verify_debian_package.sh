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

expect_file() {
    local path="$1"
    grep -Fqx "./${path}" package-files.txt || fail "Package is missing /${path}."
}

[[ -f "$package" ]] || fail "Debian package not found: $package"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "Invalid VERSION: $version"

expect_field_exact Package infiltrator-software
expect_field_exact Version "$version"
expect_field_exact Architecture amd64
expect_field_token Depends pkexec
expect_field_token Depends gpgv
expect_field_token Depends gettext

grep -Eq 'libxapp' <<<"$(field Depends)" || fail "Depends does not contain libxapp: $(field Depends)"
expect_field_token Provides mintupdate
expect_field_token Replaces mintupdate
expect_field_token Recommends flatpak

if grep -Eq '(^|, )mintupdate([, ]|$)' <<<"$(field Conflicts)"; then
    fail "Bootstrap release must not Conflict with mintupdate or an installed Software release may be unable to self-update."
fi

dpkg-deb --fsys-tarfile "$package" | tar -tf - > package-files.txt

for path in \
    usr/bin/infiltrator-software \
    usr/bin/infiltrator-software-tray \
    usr/bin/infiltrator-software-session-updater \
    usr/bin/infiltrator-software-cli \
    usr/bin/infiltrator-software-release-upgrade \
    usr/libexec/infiltrator-software-auto-updater \
    usr/libexec/infiltrator-software-maintenance \
    usr/libexec/infiltrator-software-helper \
    usr/libexec/infiltrator-software-update-helper \
    usr/libexec/infiltrator-software-engine \
    usr/lib/systemd/system/infiltrator-software-auto-update.service \
    usr/lib/systemd/system/infiltrator-software-auto-update.timer \
    usr/lib/systemd/system/infiltrator-software-maintenance.service \
    usr/lib/systemd/system/infiltrator-software-maintenance.timer \
    usr/share/dbus-1/services/net.ssmith.infiltrator.software.Engine.service \
    usr/share/dbus-1/interfaces/net.ssmith.infiltrator.software.Engine.xml \
    usr/share/polkit-1/actions/net.ssmith.infiltrator.software.policy \
    usr/share/polkit-1/actions/net.ssmith.infiltrator.software.updates.policy \
    etc/xdg/autostart/infiltrator-software-session-updater.desktop \
    etc/xdg/autostart/infiltrator-software-tray.desktop \
    usr/share/icons/hicolor/scalable/status/infiltrator-software-up-to-date-symbolic.svg \
    usr/share/icons/hicolor/scalable/status/infiltrator-software-updates-available-symbolic.svg \
    usr/share/icons/hicolor/scalable/status/infiltrator-software-checking-symbolic.svg \
    usr/share/icons/hicolor/scalable/status/infiltrator-software-installing-symbolic.svg \
    usr/share/icons/hicolor/scalable/status/infiltrator-software-error-symbolic.svg \
    usr/share/applications/net.ssmith.infiltrator.software.desktop \
    usr/share/icons/hicolor/scalable/apps/net.ssmith.infiltrator.software.svg \
    usr/share/icons/hicolor/scalable/apps/infiltrator-software.svg \
    usr/share/app-install/icons/infiltrator-software.svg \
    usr/share/metainfo/infiltrator-software.metainfo.xml \
    usr/share/fonts/truetype/infiltrator-software/mb_corpo_a_cond_regular.ttf \
    usr/share/fonts/truetype/infiltrator-software/mb_corpo_s_bold.ttf \
    usr/share/fonts/truetype/infiltrator-software/mb_corpo_s_regular.ttf; do
    expect_file "$path"
done

if grep -Eq '^\./usr/include/infiltratr/|^\./usr/lib/.*/libinfiltratr|/cmake/InfiltratrCommon/' package-files.txt; then
    fail "Software package leaked Common development/runtime payloads."
fi

root="$(mktemp -d)"
trap 'rm -rf "$root" package-files.txt' EXIT
dpkg-deb -x "$package" "$root"

desktop="$root/usr/share/applications/net.ssmith.infiltrator.software.desktop"
grep -Fqx 'StartupWMClass=net.ssmith.infiltrator.software' "$desktop" ||
    fail "Installed desktop file has the wrong StartupWMClass."
grep -Fqx 'Icon=net.ssmith.infiltrator.software' "$desktop" ||
    fail "Installed desktop file has the wrong icon name."

# Behaviour belongs to executable tests, not source-text greps. These checks
# remain valid if implementation files/functions are reorganised.
./build/infiltrator-software-cli --version | grep -Fqx "$version" ||
    fail "CLI version does not match VERSION."
./build/infiltrator-software-cli --help | grep -Fq 'upgrade' ||
    fail "CLI help no longer exposes the upgrade command."

echo "Verified Debian package contract: $package"
