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
    local common_version common_commit
    common_version="$(sed -n 's/.*INFILTRATR_COMMON_EXPECTED_VERSION "\([^"]*\)".*/\1/p' CMakeLists.txt)"
    common_commit="$(sed -n 's/.*INFILTRATR_COMMON_EXPECTED_COMMIT "\([^"]*\)".*/\1/p' CMakeLists.txt)"
    [[ -n "$common_version" ]] || die "Common expected version is missing from CMakeLists.txt."
    [[ -n "$common_commit" ]] || die "Common expected commit is missing from CMakeLists.txt."
    [[ "$(tr -d '[:space:]' < src/infiltratr-common/VERSION)" == "$common_version" ]] || \
        die "Common VERSION does not match the source contract."
    [[ "$(git -C src/infiltratr-common rev-parse HEAD)" == "$common_commit" ]] || \
        die "Common gitlink does not match the source contract."
}

tag_sha() {
    git rev-list -n 1 "v$1" 2>/dev/null || true
}

expected_assets() {
    local version="$1"
    printf '%s\n' \
        LICENSE.txt \
        SHA256SUMS.txt \
        "software_${version}_amd64.deb" \
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

    git fetch --force --tags origin
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
    local out root epoch

    require_command rsync
    require_command tar
    require_command gzip
    assert_source_version "$version"
    assert_head "$expected_sha"

    out="${REPO_ROOT}/dist/release"
    root="${temp_root}/software-source/Software-${version}"
    rm -rf "${REPO_ROOT}/dist" "${temp_root}/software-source"
    mkdir -p "$out" "$root"

    # Capture source before the build so generated files can never leak into the
    # release archive. VCS metadata and release output are the only exclusions.
    rsync -a --exclude='.git' --exclude='dist' ./ "$root/"

    epoch="$(git show -s --format=%ct "$expected_sha")"
    find "$root" -exec touch -h -d "@$epoch" {} +
    tar --sort=name \
        --mtime="@$epoch" \
        --owner=0 --group=0 --numeric-owner \
        -C "$(dirname "$root")" \
        -cf - "$(basename "$root")" \
        | gzip -n > "${out}/software_${version}_source.tar.gz"
    cp LICENSE "${out}/LICENSE.txt"
}

complete_bundle() {
    local version="$1"
    local out package

    require_command sha256sum
    assert_source_version "$version"
    out="${REPO_ROOT}/dist/release"
    package="$(dirname "$REPO_ROOT")/infiltrator-software_${version}_amd64.deb"
    [[ -f "$package" ]] || die "Verified Debian package is missing: $package"
    [[ -f "${out}/software_${version}_source.tar.gz" ]] || \
        die "Source bundle is missing before completion."
    [[ -f "${out}/LICENSE.txt" ]] || die "Release licence file is missing."

    cp "$package" "${out}/software_${version}_amd64.deb"
    (
        cd "$out"
        sha256sum \
            "software_${version}_amd64.deb" \
            "software_${version}_source.tar.gz" \
            LICENSE.txt > SHA256SUMS.txt
    )
}

verify_bundle() {
    local version="$1"
    local expected_sha="$2"
    local release_dir="$3"
    local existing_tag_sha actual_files expected_files package

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

    expected_files="$(expected_assets "$version")"
    actual_files="$(find "$release_dir" -maxdepth 1 -type f -printf '%f\n' | sort)"
    [[ "$actual_files" == "$expected_files" ]] || \
        die "Verified release bundle has an unexpected file set."

    (
        cd "$release_dir"
        sha256sum -c SHA256SUMS.txt
    )

    package="${release_dir}/software_${version}_amd64.deb"
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
    local existing_tag_sha actual_assets expected

    require_command gh
    require_command git
    [[ -n "${GH_TOKEN:-}" ]] || die "GH_TOKEN is required for GitHub publication."
    verify_bundle "$version" "$expected_sha" "$release_dir"
    expected="$(expected_assets "$version")"

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
            "${release_dir}/software_${version}_amd64.deb" \
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

publish_apt() {
    local version="$1"
    local release_sha="$2"
    local repository="$3"
    local catalogue='https://infiltrator-projects.github.io/Infiltrator-Repository/catalogue/apps.json'
    local payload attempt

    require_command curl
    require_command jq

    published() {
        local cache_buster="$1"
        curl -fsS --retry 2 --retry-delay 1 \
            "${catalogue}?release=${release_sha}&attempt=${cache_buster}" \
            | jq -e \
                --arg id software \
                --arg version "$version" \
                --arg package infiltrator-software \
                --arg asset "software_${version}_amd64.deb" \
                '.[] | select(.id == $id and .version == $version and .package == $package and .asset == $asset)' \
              >/dev/null
    }

    if published 0; then
        echo "APT repository already advertises Software $version."
        return 0
    fi

    if [[ -n "${DISPATCH_TOKEN:-}" ]]; then
        payload="$(jq -nc \
            --arg repository "$repository" \
            --arg sha "$release_sha" \
            '{event_type:"application-release",client_payload:{repository:$repository,sha:$sha}}')"
        if curl --fail-with-body --silent --show-error \
            --request POST \
            --header "Authorization: Bearer $DISPATCH_TOKEN" \
            --header "Accept: application/vnd.github+json" \
            --header "X-GitHub-Api-Version: 2022-11-28" \
            --data "$payload" \
            https://api.github.com/repos/Infiltrator-Projects/Infiltrator-Repository/dispatches; then
            echo "Central APT publication dispatch accepted."
        else
            echo "::warning::APT dispatch failed; scheduled repository recovery will be used."
        fi
    else
        echo "::warning::APT dispatch token unavailable; scheduled repository recovery will be used."
    fi

    for attempt in $(seq 1 120); do
        if published "$attempt"; then
            echo "APT repository advertises Software $version."
            return 0
        fi
        sleep 10
    done

    die "Central APT repository did not advertise Software $version within 20 minutes."
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
  publish-apt VERSION SHA REPOSITORY
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
    publish-apt) publish_apt "$@" ;;
    -h|--help|help) usage ;;
    *)
        usage >&2
        die "Unknown release command: $command"
        ;;
esac
