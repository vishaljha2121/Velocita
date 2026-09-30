#!/usr/bin/env bash
set -Eeuo pipefail

usage() { printf 'Usage: source-manifest.sh --root REPOSITORY --manifest OUTPUT\n'; }
root=
manifest=
while [[ $# -gt 0 ]]; do
    case "$1" in
        --root) root=${2:-}; shift 2 ;;
        --manifest) manifest=${2:-}; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 64 ;;
    esac
done
[[ -n $root && -n $manifest ]] || { usage >&2; exit 64; }
root=$(cd "$root" && pwd -P)
manifest_parent=$(cd "$(dirname "$manifest")" && pwd -P)
manifest="$manifest_parent/$(basename "$manifest")"
[[ ! -d "$manifest" ]] || {
    printf 'Manifest output must not be a directory: %s\n' "$manifest" >&2
    exit 65
}
case "$manifest" in
    "$root/CMakeLists.txt"|"$root/include"|"$root/include/"*|\
    "$root/src"|"$root/src/"*|"$root/tests"|"$root/tests/"*|\
    "$root/deploy"|"$root/deploy/"*|"$root/cmake"|"$root/cmake/"*)
        printf 'Manifest output must be outside selected source trees: %s\n' \
            "$manifest" >&2
        exit 65
        ;;
esac

for required in CMakeLists.txt include src tests deploy; do
    [[ -e "$root/$required" ]] || {
        printf 'Required manifest input is missing: %s\n' "$required" >&2
        exit 65
    }
done

[[ ! -L "$root/CMakeLists.txt" ]] || {
    printf 'Symlinks are not allowed in manifest inputs: CMakeLists.txt\n' >&2
    exit 65
}

for selected_tree in include src tests deploy cmake; do
    [[ -e "$root/$selected_tree" ]] || continue
    symlink=$(find "$root/$selected_tree" -type l -print -quit)
    [[ -z $symlink ]] || {
        printf 'Symlinks are not allowed in manifest inputs: %s\n' \
            "${symlink#"$root"/}" >&2
        exit 65
    }
done

if command -v sha256sum >/dev/null 2>&1; then
    hash_file() { sha256sum "$1" | awk '{print $1}'; }
    hash_stream() { sha256sum | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then
    hash_file() { shasum -a 256 "$1" | awk '{print $1}'; }
    hash_stream() { shasum -a 256 | awk '{print $1}'; }
else
    printf 'sha256sum or shasum is required\n' >&2
    exit 69
fi

path_list=$(mktemp "${TMPDIR:-/tmp}/velocita-manifest.XXXXXX")
record_list=$(mktemp "${TMPDIR:-/tmp}/velocita-manifest-records.XXXXXX")
publish_temp=
cleanup() {
    rm -f "$path_list" "$record_list"
    [[ -z $publish_temp ]] || rm -f "$publish_temp"
}
trap cleanup EXIT

(
    cd "$root"
    printf '%s\0' CMakeLists.txt
    find include src -type f -print0
    [[ ! -d cmake ]] || find cmake -type f -print0
    find tests -maxdepth 1 -type f -print0
    find deploy -type f -print0
) | LC_ALL=C sort -zu >"$path_list"

while IFS= read -r -d '' relative; do
    [[ $relative != *$'\n'* ]] || {
        printf 'Newlines are not allowed in manifest paths: %q\n' "$relative" >&2
        exit 65
    }
    if ! digest=$(hash_file "$root/$relative"); then
        printf 'Failed to hash manifest input: %s\n' "$relative" >&2
        exit 74
    fi
    [[ $digest =~ ^[0-9a-f]{64}$ ]] || {
        printf 'Invalid SHA-256 digest for manifest input: %s\n' "$relative" >&2
        exit 74
    }
    printf '%s  %s\n' "$digest" "$relative"
done <"$path_list" >"$record_list"

if ! root_digest=$(hash_stream <"$record_list"); then
    printf 'Failed to hash source manifest\n' >&2
    exit 74
fi
[[ $root_digest =~ ^[0-9a-f]{64}$ ]] || {
    printf 'Invalid source-root SHA-256 digest\n' >&2
    exit 74
}
publish_temp=$(mktemp "$manifest_parent/.source-manifest.XXXXXX")
cp "$record_list" "$publish_temp"
mv -f "$publish_temp" "$manifest"
publish_temp=
printf '%s\n' "$root_digest"
