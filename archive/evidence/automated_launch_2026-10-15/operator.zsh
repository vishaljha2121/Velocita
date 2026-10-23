#!/bin/zsh
# Owner-run operator commands for the new incarnation. Dry run unless the last argument is --run.
#   operator.zsh INCARNATION ceremony [--run]                  sequences 1-5
#   operator.zsh INCARNATION funding RECORD SHA256 [--run]     sequence 6
#   operator.zsh INCARNATION mode normal|halted SEQ [--run]
set -euo pipefail
cd "$HOME/aeron-raft-pi"
I=$1; shift
[[ "$I" =~ '^[0-9a-f]{32}$' && "$I" != b79e65be1ea14e219016ec48ea02efef ]] || { print -u2 'new incarnation required'; exit 64; }
C="$HOME/.cache/velocita-build/automated-launch-54a5fcb/mac/velocita-v2-client"
S="$HOME/.cache/velocita-operator/$I"
umask 077
mkdir -p "$S"
A=(--caller-key-id 9 --caller-id 7 --key-stdin --incarnation "$I"
 --session-file "$S/client.session" --response-endpoint 100.91.80.64:44400
 --node 1=100.72.127.102:44301 --node 2=100.84.151.116:44302 --node 3=100.85.36.22:44303
 --aeron-dir "$S/aeron")
RUN=0; [[ "${@[-1]:-}" == --run ]] && { RUN=1; set -- "${@[1,-2]}"; }
send() {
  local seq=$1; shift
  if (( RUN )); then tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence "$seq" "$@"
  else "$C" "${A[@]}" --dry-run --sequence "$seq" "$@"; fi
}
case "$1" in
  ceremony)
    (( $# == 1 )) || exit 64
    send 1 kill-epoch 1
    send 2 executor-generation 1
    send 3 install-catalog 3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe 1 1
    send 4 assign-role 22 gateway
    send 5 assign-role 21 strategy ;;
  funding)
    (( $# == 3 )) || exit 64
    [[ "$(shasum -a 256 "$2" | cut -d ' ' -f 1)" == "$3" ]] || { print -u2 'opening record differs from its pin'; exit 65; }
    send 6 spot-swap-record "$2" ;;
  mode)
    (( $# == 3 )) && [[ "$2" == normal || "$2" == halted ]] && [[ "$3" =~ '^[0-9]+$' ]] && (( $3 >= 7 )) || exit 64
    send "$3" strategy-mode "$2" 1 1 ;;
  *) exit 64 ;;
esac
