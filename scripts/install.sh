#!/bin/sh
# Install an extracted Linux or macOS package for the current user.
set -eu
fail() { printf '%s\n' "Install failed: $*" >&2; exit 1; }
package_dir=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
prefix=${HOME:?HOME is not set}/.local
while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix) [ "$#" -ge 2 ] || fail "--prefix needs a directory"; prefix=$2; shift 2 ;;
        --help) printf '%s\n' 'Usage: sh install.sh [--prefix DIRECTORY]'; exit 0 ;;
        *) fail "Unknown option: $1" ;;
    esac
done
[ -n "$prefix" ] || fail "Empty install prefix"
case "$prefix" in /*) ;; *) prefix=$PWD/$prefix ;; esac
[ "$prefix" != / ] || fail "The install prefix cannot be /"
[ -f "$package_dir/BUILD_INFO" ] || fail "Run the installer from an extracted release package"
platform=$(sed -n 's/^platform=//p' "$package_dir/BUILD_INFO")
arch=$(sed -n 's/^arch=//p' "$package_dir/BUILD_INFO")
version=$(sed -n 's/^version=//p' "$package_dir/BUILD_INFO")
case "$(uname -s)" in Linux) host=linux ;; Darwin) host=macos ;; *) fail "Use the Windows installer on Windows" ;; esac
case "$(uname -m)" in x86_64|amd64) machine=x86_64 ;; arm64|aarch64) machine=arm64 ;; *) fail "Unsupported processor" ;; esac
if [ "$host" = macos ] && [ "$(/usr/sbin/sysctl -n hw.optional.arm64 2>/dev/null || true)" = 1 ]; then
    machine=arm64
fi
[ "$host/$machine" = "$platform/$arch" ] || fail "Package is for $platform/$arch; this computer is $host/$machine"
[ -f "$package_dir/bin/blacksquelch" ] || fail "Package executable is missing"
actual=$("$package_dir/bin/blacksquelch" --version) || fail "The executable cannot run on this system"
[ "$actual" = "BlackSquelch $version" ] || fail "Package version does not match its executable"
for name in README.md LICENSE-miniaudio.txt BUILD_INFO uninstall.sh; do
    [ -f "$package_dir/$name" ] || fail "Package is missing $name"
done
mkdir -p "$prefix"
prefix=$(CDPATH= cd "$prefix" && pwd -P)
[ "$prefix" != / ] || fail "The install prefix cannot be /"
[ ! -d "$prefix/bin/blacksquelch" ] || fail "The executable destination is a directory"
stage=$(mktemp -d "$prefix/.blacksquelch-install.XXXXXX")
trap 'rm -rf "$stage"' 0
trap 'exit 1' HUP INT TERM
install -m 755 "$package_dir/bin/blacksquelch" "$stage/blacksquelch"
mkdir -p "$prefix/bin" "$prefix/share/blacksquelch"
for name in README.md LICENSE-miniaudio.txt BUILD_INFO uninstall.sh; do
    install -m 644 "$package_dir/$name" "$prefix/share/blacksquelch/$name"
done
# Rename on the destination filesystem, so updates never expose a partial executable.
mv -f "$stage/blacksquelch" "$prefix/bin/blacksquelch"
printf '%s\n' "Installed BlackSquelch $version" "Run: $prefix/bin/blacksquelch" \
    "Uninstall: sh \"$prefix/share/blacksquelch/uninstall.sh\""
case ":${PATH:-}:" in *":$prefix/bin:"*) ;; *) printf '%s\n' "Optional: add $prefix/bin to PATH." ;; esac
