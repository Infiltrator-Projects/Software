#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly REPO_ROOT
cd "$REPO_ROOT"

die() {
    printf '::error::%s\n' "$*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

source_version() {
    [[ -f VERSION ]] || die "VERSION is missing."
    local version
    version="$(tr -d '[:space:]' < VERSION)"
    [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || \
        die "VERSION must be semantic x.y.z; got '$version'."
    printf '%s\n' "$version"
}

assert_source_version() {
    local expected="$1"
    local actual
    actual="$(source_version)"
    [[ "$actual" == "$expected" ]] || \
        die "Source VERSION is $actual; expected $expected."
}

assert_head() {
    local expected_sha="$1"
    local actual_sha
    actual_sha="$(git rev-parse HEAD)"
    [[ "$actual_sha" == "$expected_sha" ]] || \
        die "Checked-out commit is $actual_sha; expected $expected_sha."
}

assert_common_pin() {
    local common_version gitlink_commit checkout_commit
    [[ -f src/infiltratr-common/VERSION ]] || die "Common VERSION is missing."
    common_version="$(tr -d '[:space:]' < src/infiltratr-common/VERSION)"
    [[ "$common_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || \
        die "Common VERSION must be semantic x.y.z; got '$common_version'."

    gitlink_commit="$(git rev-parse 'HEAD:src/infiltratr-common' 2>/dev/null)" || \
        die "Unable to resolve the Common gitlink from HEAD."
    checkout_commit="$(git -C src/infiltratr-common rev-parse HEAD 2>/dev/null)" || \
        die "Unable to resolve the checked-out Common commit."
    [[ "$checkout_commit" == "$gitlink_commit" ]] || \
        die "Common checkout $checkout_commit does not match gitlink $gitlink_commit."
}

tag_sha() {
    git rev-list -n 1 "v$1" 2>/dev/null || true
}

package_architecture() {
    local package="$1"
    local architecture
    architecture="$(dpkg-deb -f "$package" Architecture 2>/dev/null)" || \
        die "Unable to read Debian package architecture: $package"
    [[ "$architecture" =~ ^[A-Za-z0-9][A-Za-z0-9-]*$ ]] || \
        die "Invalid Debian package architecture '$architecture'."
    printf '%s\n' "$architecture"
}

built_package() {
    local version="$1"
    local parent
    local -a packages
    parent="$(dirname "$REPO_ROOT")"
    packages=("$parent"/infiltrator-software_"${version}"_*.deb)
    if [[ ${#packages[@]} -ne 1 || ! -f "${packages[0]}" ]]; then
        die "Expected exactly one built Debian package for Software ${version}."
    fi
    printf '%s\n' "${packages[0]}"
}

release_package() {
    local version="$1"
    local release_dir="$2"
    local -a packages
    packages=("$release_dir"/software_"${version}"_*.deb)
    if [[ ${#packages[@]} -ne 1 || ! -f "${packages[0]}" ]]; then
        die "Expected exactly one release Debian package for Software ${version}."
    fi
    printf '%s\n' "${packages[0]}"
}

expected_assets() {
    local version="$1"
    local package_name="$2"
    printf '%s\n' \
        LICENSE.txt \
        SHA256SUMS.txt \
        "$package_name" \
        "software_${version}_source.tar.gz" | sort
}

identity() {
    local event_name="$1"
    local ref="$2"
    local expected_sha="$3"
    local output_file="${4:-}"
    local version release existing_tag_sha

    require_command git
    version="$(source_version)"
    assert_head "$expected_sha"
    assert_common_pin

    release=false
    case "${event_name}:${ref}" in
        push:refs/heads/main|workflow_dispatch:refs/heads/main|workflow_dispatch:refs/tags/v${version})
            release=true
            ;;
        workflow_dispatch:*)
            die "Manual release recovery must target main or refs/tags/v${version}; got ${ref}."
            ;;
    esac

    if [[ "$release" == true ]]; then
        # Pull tag state only for publication/recovery. Pull requests and other
        # verification-only runs do not need an extra network dependency.
        git fetch --force --tags origin
        existing_tag_sha="$(tag_sha "$version")"
        if [[ -n "$existing_tag_sha" && "$existing_tag_sha" != "$expected_sha" ]]; then
            die "Version ${version} is already tagged at ${existing_tag_sha}; current commit is ${expected_sha}."
        fi
    fi

    if [[ -n "$output_file" ]]; then
        printf 'version=%s\nrelease=%s\n' "$version" "$release" >> "$output_file"
    else
        printf 'version=%s\nrelease=%s\n' "$version" "$release"
    fi
}

source_bundle() {
    local version="$1"
    local expected_sha="$2"
    local temp_root="$3"
    local out root staging epoch

    require_command git
    require_command tar
    require_command gzip
    assert_source_version "$version"
    assert_head "$expected_sha"
    assert_common_pin

    out="${REPO_ROOT}/dist/release"
    staging="${temp_root}/software-source"
    root="${staging}/Software-${version}"
    rm -rf "${REPO_ROOT}/dist" "$staging"
    mkdir -p "$out" "$staging"

    # Build the source archive from Git objects rather than the working tree.
    # Local build directories, editor files and other untracked state therefore
    # cannot leak into a release even when this command is run outside CI.
    git archive --format=tar --prefix="Software-${version}/" "$expected_sha" \
        | tar -xf - -C "$staging"
    mkdir -p "$root/src/infiltratr-common"
    git -C src/infiltratr-common archive --format=tar HEAD \
        | tar -xf - -C "$root/src/infiltratr-common"

    epoch="$(git show -s --format=%ct "$expected_sha")"
    find "$root" -exec touch -h -d "@$epoch" {} +
    tar --sort=name \
        --mtime="@$epoch" \
        --owner=0 --group=0 --numeric-owner \
        -C "$staging" \
        -cf - "Software-${version}" \
        | gzip -n > "${out}/software_${version}_source.tar.gz"
    cp LICENSE "${out}/LICENSE.txt"
}

complete_bundle() {
    local version="$1"
    local out package architecture release_name

    require_command sha256sum
    require_command dpkg-deb
    assert_source_version "$version"
    out="${REPO_ROOT}/dist/release"
    package="$(built_package "$version")"
    architecture="$(package_architecture "$package")"
    release_name="software_${version}_${architecture}.deb"
    [[ -f "${out}/software_${version}_source.tar.gz" ]] || \
        die "Source bundle is missing before completion."
    [[ -f "${out}/LICENSE.txt" ]] || die "Release licence file is missing."

    cp "$package" "${out}/${release_name}"
    (
        cd "$out"
        sha256sum \
            "$release_name" \
            "software_${version}_source.tar.gz" \
            LICENSE.txt > SHA256SUMS.txt
    )
}

verify_bundle() {
    local version="$1"
    local expected_sha="$2"
    local release_dir="$3"
    local existing_tag_sha actual_files expected_files package architecture package_name

    require_command git
    require_command sha256sum
    require_command dpkg-deb
    assert_source_version "$version"
    assert_head "$expected_sha"

    git fetch --force --tags origin
    existing_tag_sha="$(tag_sha "$version")"
    if [[ -n "$existing_tag_sha" && "$existing_tag_sha" != "$expected_sha" ]]; then
        die "Existing tag v${version} points to ${existing_tag_sha}; expected ${expected_sha}."
    fi

    package="$(release_package "$version" "$release_dir")"
    architecture="$(package_architecture "$package")"
    package_name="software_${version}_${architecture}.deb"
    [[ "$(basename "$package")" == "$package_name" ]] || \
        die "Release Debian package filename does not match its architecture."

    expected_files="$(expected_assets "$version" "$package_name")"
    actual_files="$(find "$release_dir" -maxdepth 1 -type f -printf '%f\n' | sort)"
    [[ "$actual_files" == "$expected_files" ]] || \
        die "Verified release bundle has an unexpected file set."

    (
        cd "$release_dir"
        sha256sum -c SHA256SUMS.txt
    )

    [[ "$(dpkg-deb -f "$package" Package)" == "infiltrator-software" ]] || \
        die "Release Debian package identity is incorrect."
    [[ "$(dpkg-deb -f "$package" Version)" == "$version" ]] || \
        die "Release Debian package version is incorrect."
}

publish_github() {
    local version="$1"
    local expected_sha="$2"
    local release_dir="$3"
    local repository="$4"
    local tag="v${version}"
    local existing_tag_sha actual_assets expected package package_name

    require_command gh
    require_command git
    [[ -n "${GH_TOKEN:-}" ]] || die "GH_TOKEN is required for GitHub publication."
    verify_bundle "$version" "$expected_sha" "$release_dir"
    package="$(release_package "$version" "$release_dir")"
    package_name="$(basename "$package")"
    expected="$(expected_assets "$version" "$package_name")"

    if gh release view "$tag" --repo "$repository" >/dev/null 2>&1; then
        git fetch --force --tags origin
        existing_tag_sha="$(tag_sha "$version")"
        [[ "$existing_tag_sha" == "$expected_sha" ]] || \
            die "Existing release $tag points to ${existing_tag_sha:-<missing>}; expected $expected_sha."
    else
        gh release create "$tag" \
            --repo "$repository" \
            --target "$expected_sha" \
            --title "Software ${version}" \
            --generate-notes \
            --latest
    fi

    actual_assets="$(gh release view "$tag" --repo "$repository" \
        --json assets --jq '.assets[].name' | sort)"
    if [[ "$actual_assets" != "$expected" ]]; then
        # Recovery from a previous partial upload is intentionally idempotent.
        gh release upload "$tag" \
            "$package" \
            "${release_dir}/software_${version}_source.tar.gz" \
            "${release_dir}/LICENSE.txt" \
            "${release_dir}/SHA256SUMS.txt" \
            --repo "$repository" \
            --clobber
        actual_assets="$(gh release view "$tag" --repo "$repository" \
            --json assets --jq '.assets[].name' | sort)"
    fi

    [[ "$actual_assets" == "$expected" ]] || \
        die "Release $tag has an unexpected asset set."
}

usage() {
    cat <<'USAGE'
Usage: tools/release.sh COMMAND [arguments]

Commands:
  identity EVENT REF SHA [GITHUB_OUTPUT]
  source-bundle VERSION SHA TEMP_ROOT
  complete-bundle VERSION
  verify-bundle VERSION SHA RELEASE_DIR
  publish-github VERSION SHA RELEASE_DIR REPOSITORY
USAGE
}

command="${1:-}"
if [[ -z "$command" ]]; then
    usage
    exit 2
fi
shift

case "$command" in
    identity) identity "$@" ;;
    source-bundle) source_bundle "$@" ;;
    complete-bundle) complete_bundle "$@" ;;
    verify-bundle) verify_bundle "$@" ;;
    publish-github) publish_github "$@" ;;
    -h|--help|help) usage ;;
    *)
        usage >&2
        die "Unknown release command: $command"
        ;;
esac