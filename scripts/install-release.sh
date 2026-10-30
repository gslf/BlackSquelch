#!/bin/sh
# Download a release, verify SHA-256, then run its installer.
set -eu
fail() { printf '%s\n' "Install failed: $*" >&2; exit 1; }
repo=https://github.com/gslf/BlackSquelch
version=latest
prefix=${HOME:?HOME is not set}/.local
while [ "$#" -gt 0 ]; do
    case "$1" in
        --version) [ "$#" -ge 2 ] || fail "--version needs vMAJOR.MINOR.PATCH"; version=$2; shift 2 ;;
        --prefix) [ "$#" -ge 2 ] || fail "--prefix needs a directory"; prefix=$2; shift 2 ;;
        --help) printf '%s\n' 'Usage: sh install-release.sh [--version v1.0.0] [--prefix DIRECTORY]'; exit 0 ;;
        *) fail "Unknown option: $1" ;;
    esac
done
command -v curl >/dev/null 2>&1 || fail "curl is required"
command -v tar >/dev/null 2>&1 || fail "tar is required"
case "$(uname -s)" in Linux) platform=linux ;; Darwin) platform=macos ;; *) fail "Use install-release.ps1 on Windows" ;; esac
case "$(uname -m)" in x86_64|amd64) arch=x86_64 ;; arm64|aarch64) arch=arm64 ;; *) fail "Unsupported processor" ;; esac
if [ "$platform" = macos ] && [ "$(/usr/sbin/sysctl -n hw.optional.arm64 2>/dev/null || true)" = 1 ]; then
    arch=arm64
fi
if [ "$version" = latest ]; then
    resolved=$(curl --fail --silent --show-error --location --proto '=https' --tlsv1.2 \
        --output /dev/null --write-out '%{url_effective}' "$repo/releases/latest")
    case "$resolved" in "$repo/releases/tag/"*) version=${resolved##*/} ;; *) fail "Cannot find the latest release" ;; esac
fi
printf '%s\n' "$version" | LC_ALL=C grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$' || fail "Invalid release version"
asset=blacksquelch-$platform-$arch.tar.gz
base=$repo/releases/download/$version
temporary=$(mktemp -d "${TMPDIR:-/tmp}/blacksquelch-download.XXXXXX")
trap 'rm -rf "$temporary"' 0
trap 'exit 1' HUP INT TERM
for file in checksums.sha256 "$asset"; do
    curl --fail --silent --show-error --location --proto '=https' --tlsv1.2 \
        "$base/$file" --output "$temporary/$file"
done
expected=$(awk -v name="$asset" '$2 == name {print $1}' "$temporary/checksums.sha256")
printf '%s\n' "$expected" | LC_ALL=C grep -Eq '^[0-9a-f]{64}$' || fail "Missing or invalid checksum"
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$temporary/$asset" | awk '{print $1}')
elif command -v shasum >/dev/null 2>&1; then
    actual=$(shasum -a 256 "$temporary/$asset" | awk '{print $1}')
else
    fail "sha256sum or shasum is required"
fi
[ "$actual" = "$expected" ] || fail "Checksum mismatch; nothing was installed"
# Require the expected package root before extracting downloaded content.
tar -tzf "$temporary/$asset" | awk '
    $0 != "blacksquelch" && $0 !~ /^blacksquelch\// {exit 1}
    /(^|\/)\.\.(\/|$)/ {exit 1}
' || fail "Invalid archive paths"
tar -xzf "$temporary/$asset" -C "$temporary"
sh "$temporary/blacksquelch/install.sh" --prefix "$prefix"
