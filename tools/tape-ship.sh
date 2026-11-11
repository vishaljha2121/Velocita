#!/bin/sh
# Ships one committed tree to a host as ~/velocita-tape-release-<short>, and checks it arrived byte-identical.
set -eu
[ $# -eq 2 ] || { echo "usage: $0 COMMIT HOST" >&2; exit 64; }
commit=$(git rev-parse --verify "$1^{commit}")
host=$2
short=$(printf %.7s "$commit")
dest="velocita-tape-release-$short"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
git archive --format=tar --prefix="$dest/" "$commit" > "$work/tree.tar"
mkdir "$work/extra" "$work/extra/$dest"
printf '%s\n' "$commit" > "$work/extra/$dest/SOURCE_COMMIT"
COPYFILE_DISABLE=1 tar --no-xattrs -C "$work/extra" -rf "$work/tree.tar" "$dest/SOURCE_COMMIT"
local_sum=$(shasum -a 256 "$work/tree.tar" | cut -d' ' -f1)
ssh "$host" "set -eu; [ ! -e \"\$HOME/$dest\" ] || { echo 'tape-ship: $dest already exists' >&2; exit 65; }
    t=\$(mktemp); cat > \"\$t\"; s=\$(sha256sum \"\$t\" | cut -d' ' -f1)
    [ \"\$s\" = $local_sum ] || { rm -f \"\$t\"; echo \"tape-ship: checksum \$s != $local_sum\" >&2; exit 65; }
    tar -C \"\$HOME\" -xf \"\$t\"; rm -f \"\$t\"; cat \"\$HOME/$dest/SOURCE_COMMIT\"" < "$work/tree.tar"
echo "tape-ship: $host:~/$dest = $commit (tar sha256 $local_sum)"
