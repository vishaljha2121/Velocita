#!/usr/bin/env bash
set -euo pipefail

config=${1:?node config}
secrets=${2:?secrets directory}
output=${3:?output .prom file}
node=${VELOCITA_NODE:-}
ssh_host=${VELOCITA_SSH_HOST:-$(hostname)}

names=""
if [[ -r $config ]]; then
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
