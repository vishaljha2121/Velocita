#!/bin/sh
# Owner-run key ceremony for the tape: the HMAC master secret lives in the Mac Keychain and reaches hosts only
# in tmpfs; each voter's Ed25519 head key is generated on its own host and only the public half leaves it.
#   tape-key-ceremony.sh master          create Keychain item tape-master (once)
#   tape-key-ceremony.sh heads TAG       make head keys on the three hosts with image TAG, collect and share publics
#   tape-key-ceremony.sh push            copy tape-master into each host's tmpfs (after each host boot)
#   tape-key-ceremony.sh fingerprints    print the SHA-256 of each collected public key
set -eu
cd "$(dirname "$0")/.."
hosts="1:velocitas@100.72.127.102 2:velocitas@100.84.151.116 3:velocitas@100.85.36.22"
public_dir="$HOME/.config/velocita-tape/public"

fingerprints() {
    for n in 1 2 3; do
        f="$public_dir/head-$n.pub.pem"
        [ -f "$f" ] || { echo "voter $n: missing $f" >&2; return 65; }
        printf 'voter %s  %s\n' "$n" "$(openssl pkey -pubin -in "$f" -outform DER | shasum -a 256 | cut -d' ' -f1)"
    done
}

case "${1:-}" in
master)
    [ $# -eq 1 ] || exit 64
    tools/velocita-secrets init tape-master
    echo "tape-key-ceremony: Keychain item tape-master created"
    ;;
heads)
    [ $# -eq 2 ] || exit 64
    tag=$2
    mkdir -p "$public_dir"
    for entry in $hosts; do
        n=${entry%%:*} host=${entry#*:}
        [ ! -e "$public_dir/head-$n.pub.pem" ] || { echo "tape-key-ceremony: $public_dir/head-$n.pub.pem exists" >&2; exit 65; }
        ssh "$host" "set -eu; mkdir -p \$HOME/velocita-tape/config/public; chmod 700 \$HOME/velocita-tape/config
            [ ! -e \$HOME/velocita-tape/config/head-$n.pem ] || { echo 'head-$n.pem exists on $host' >&2; exit 65; }
            podman run --rm --network none --userns=keep-id --read-only -v \$HOME/velocita-tape/config:/k \
                localhost/velocita-tape:$tag velocita-tape-keys head /k/head-$n.pem /k/public/head-$n.pub.pem
            cat \$HOME/velocita-tape/config/public/head-$n.pub.pem" > "$public_dir/head-$n.pub.pem.tmp"
        mv "$public_dir/head-$n.pub.pem.tmp" "$public_dir/head-$n.pub.pem"
    done
    for entry in $hosts; do
        host=${entry#*:}
        for n in 1 2 3; do
            ssh "$host" "umask 022; cat > \$HOME/velocita-tape/config/public/head-$n.pub.pem.tmp && mv \$HOME/velocita-tape/config/public/head-$n.pub.pem.tmp \$HOME/velocita-tape/config/public/head-$n.pub.pem" \
                < "$public_dir/head-$n.pub.pem"
        done
    done
    fingerprints
    ;;
push)
    [ $# -eq 1 ] || exit 64
    for entry in $hosts; do
        host=${entry#*:}
        path=$(ssh "$host" 'printf %s "/run/user/$(id -u)/velocita-tape-secrets/master.key"')
        tools/velocita-secrets push tape-master "$host" "$path"
        echo "tape-key-ceremony: tape-master pushed to $host"
    done
    ;;
fingerprints)
    fingerprints
    ;;
*)
    sed -n '2,7p' "$0" >&2
    exit 64
    ;;
esac
