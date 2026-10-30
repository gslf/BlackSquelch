#!/bin/sh
# Remove application files only. Keep preferences, macros, logs and received files.
set -eu
fail() { printf '%s\n' "Uninstall failed: $*" >&2; exit 1; }
script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
if [ "$(basename "$script_dir")" = blacksquelch ] && [ "$(basename "$(dirname "$script_dir")")" = share ]; then
    prefix=$(CDPATH= cd "$script_dir/../.." && pwd -P)
else
    prefix=${HOME:?HOME is not set}/.local
fi
while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix) [ "$#" -ge 2 ] || fail "--prefix needs a directory"; prefix=$2; shift 2 ;;
        --help) printf '%s\n' 'Usage: sh uninstall.sh [--prefix DIRECTORY]'; exit 0 ;;
        *) fail "Unknown option: $1" ;;
    esac
done
[ -n "$prefix" ] || fail "Empty install prefix"
[ -d "$prefix" ] || { printf '%s\n' 'BlackSquelch is not installed here.'; exit 0; }
prefix=$(CDPATH= cd "$prefix" && pwd -P)
[ "$prefix" != / ] || fail "The install prefix cannot be /"
rm -f "$prefix/bin/blacksquelch"
for name in README.md LICENSE-miniaudio.txt BUILD_INFO uninstall.sh; do
    rm -f "$prefix/share/blacksquelch/$name"
done
rmdir "$prefix/share/blacksquelch" 2>/dev/null || true
printf '%s\n' 'BlackSquelch removed. Preferences, macros and received files were kept.'
