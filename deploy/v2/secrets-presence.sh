#!/usr/bin/env bash
# secrets-presence: which secrets this voter's config needs, and whether each is
# in the RAM-only secrets directory, as node-exporter textfile metrics.
#
#   secrets-presence.sh NODE_CONFIG SECRETS_DIR OUTPUT_PROM
#   env: VELOCITA_NODE (node id label), VELOCITA_SSH_HOST (how the Mac reaches this host)
#
# After a reboot the secrets directory is empty by design; the node refuses to
# start and the alert VelocitaSecretsMissing names the host, the secret and the
# push command. Presence comes from the directory entry (exists, non-empty):
# this script NEVER opens a secret, and needs no secret to run.
set -euo pipefail

config=${1:?node config}
secrets=${2:?secrets directory}
output=${3:?output .prom file}
node=${VELOCITA_NODE:-}
ssh_host=${VELOCITA_SSH_HOST:-$(hostname)}

names=""
if [[ -r $config ]]; then
    # key_file = /run/velocita-secrets/NAME.key (the path inside the node's container)
    names=$(sed -n -E 's#^[[:space:]]*(voter|caller)\.[0-9]+\.key_file[[:space:]]*=[[:space:]]*/run/velocita-secrets/([A-Za-z0-9._-]+)\.key[[:space:]]*$#\2#p' "$config")
fi

tmp=$(mktemp "$(dirname "$output")/.velocita-secrets.prom.XXXXXX")
{
    echo "# TYPE velocita_secret_present gauge"
    for name in $names; do
        path=$secrets/$name.key
        [[ -s $path ]] && present=1 || present=0   # -s: stat only, never read
        echo "velocita_secret_present{secret=\"$name\",path=\"$path\",node=\"$node\",ssh_host=\"$ssh_host\"} $present"
    done
    echo "# TYPE velocita_secrets_config_ok gauge"
    echo "velocita_secrets_config_ok $([[ -n $names ]] && echo 1 || echo 0)"
    echo "# TYPE velocita_secrets_check_timestamp_seconds gauge"
    echo "velocita_secrets_check_timestamp_seconds $(date +%s)"
} > "$tmp"
chmod 0644 "$tmp"
mv -f "$tmp" "$output"
