#!/usr/bin/env bash
set -euo pipefail

output_dir="${VELOCITA_TEXTFILE_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/velocita/textfile}"
output_file="$output_dir/velocita_raft.prom"

install -d -m 0755 "$output_dir"
temporary_file="$(mktemp "$output_file.tmp.XXXXXX")"
trap 'rm -f "$temporary_file"' EXIT

now="$(date +%s)"
observer_success=1

is_integer() {
    [[ "$1" =~ ^-?[0-9]+$ ]]
}

{
    echo '# HELP velocita_raft_unit_up Whether the systemd voter service is active.'
    echo '# TYPE velocita_raft_unit_up gauge'
    echo '# HELP velocita_raft_role Current Raft role parsed from the existing status journal.'
    echo '# TYPE velocita_raft_role gauge'
    echo '# HELP velocita_raft_leader_id Current known leader ID, or -1 when unknown.'
    echo '# TYPE velocita_raft_leader_id gauge'
    echo '# HELP velocita_raft_term Current Raft term.'
    echo '# TYPE velocita_raft_term gauge'
    echo '# HELP velocita_raft_log_current_index Last local Raft log index.'
    echo '# TYPE velocita_raft_log_current_index gauge'
    echo '# HELP velocita_raft_commit_index Highest Raft log index known committed.'
    echo '# TYPE velocita_raft_commit_index gauge'
    echo '# HELP velocita_raft_applied_index Highest Raft log index applied to the state machine.'
    echo '# TYPE velocita_raft_applied_index gauge'
    echo '# HELP velocita_raft_commit_lag_entries Local log entries not yet committed.'
    echo '# TYPE velocita_raft_commit_lag_entries gauge'
    echo '# HELP velocita_raft_apply_lag_entries Committed entries not yet applied.'
    echo '# TYPE velocita_raft_apply_lag_entries gauge'
    echo '# HELP velocita_raft_counter_value Current replicated counter state.'
    echo '# TYPE velocita_raft_counter_value gauge'
    echo '# HELP velocita_raft_status_age_seconds Age of the latest parsed status record.'
    echo '# TYPE velocita_raft_status_age_seconds gauge'

    for id in 1 2 3; do
        unit="velocita-raft-$id.service"
        unit_up=0
        if systemctl --user is-active --quiet "$unit"; then
            unit_up=1
        fi
        echo "velocita_raft_unit_up{node_id=\"$id\"} $unit_up"

        line="$(journalctl --user --unit "$unit" --grep='^status node=' \
            --lines=1 --output=short-unix --no-pager --quiet 2>/dev/null || true)"
        if [[ -z "$line" ]]; then
            observer_success=0
            continue
        fi

        timestamp="${line%% *}"
        timestamp="${timestamp%%.*}"
        payload="${line#*status }"
        node_id=''
        role=''
        leader=''
        term=''
        log_range=''
        commit=''
        applied=''
        counter=''
        for field in $payload; do
            case "$field" in
                node=*) node_id="${field#node=}" ;;
                role=*) role="${field#role=}" ;;
                leader=*) leader="${field#leader=}" ;;
                term=*) term="${field#term=}" ;;
                log=*) log_range="${field#log=}" ;;
                commit=*) commit="${field#commit=}" ;;
                applied=*) applied="${field#applied=}" ;;
                counter=*) counter="${field#counter=}" ;;
            esac
        done
        current_index="${log_range#*..}"

        if [[ "$node_id" != "$id" ]] ||
           [[ ! "$role" =~ ^(none|follower|candidate|leader)$ ]] ||
           ! is_integer "$timestamp" || ! is_integer "$leader" ||
           ! is_integer "$term" || ! is_integer "$current_index" ||
           ! is_integer "$commit" || ! is_integer "$applied" ||
           ! is_integer "$counter"; then
            observer_success=0
            continue
        fi

        for possible_role in none follower candidate leader; do
            value=0
            [[ "$role" == "$possible_role" ]] && value=1
            echo "velocita_raft_role{node_id=\"$id\",role=\"$possible_role\"} $value"
        done
        echo "velocita_raft_leader_id{node_id=\"$id\"} $leader"
        echo "velocita_raft_term{node_id=\"$id\"} $term"
        echo "velocita_raft_log_current_index{node_id=\"$id\"} $current_index"
        echo "velocita_raft_commit_index{node_id=\"$id\"} $commit"
        echo "velocita_raft_applied_index{node_id=\"$id\"} $applied"
        commit_lag=$((current_index - commit))
        apply_lag=$((commit - applied))
        (( commit_lag < 0 )) && commit_lag=0
        (( apply_lag < 0 )) && apply_lag=0
        echo "velocita_raft_commit_lag_entries{node_id=\"$id\"} $commit_lag"
        echo "velocita_raft_apply_lag_entries{node_id=\"$id\"} $apply_lag"
        echo "velocita_raft_counter_value{node_id=\"$id\"} $counter"
        age=$((now - timestamp))
        (( age < 0 )) && age=0
        echo "velocita_raft_status_age_seconds{node_id=\"$id\"} $age"
    done

    echo '# HELP velocita_raft_observer_success Whether all three latest status records parsed successfully.'
    echo '# TYPE velocita_raft_observer_success gauge'
    echo "velocita_raft_observer_success $observer_success"
    echo '# HELP velocita_raft_observer_last_run_timestamp_seconds Unix time of this observer run.'
    echo '# TYPE velocita_raft_observer_last_run_timestamp_seconds gauge'
    echo "velocita_raft_observer_last_run_timestamp_seconds $now"
} > "$temporary_file"

chmod 0644 "$temporary_file"
mv -f "$temporary_file" "$output_file"
trap - EXIT
