#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-/opt/velocita-model}"
DATA_DIR="${DATA_DIR:-/var/lib/velocita-model}"
SERVICE_USER="${SERVICE_USER:-velocita}"
SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        echo "run as root: sudo $0" >&2
        exit 1
    fi
}

check_python() {
    local version
    version="$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
    if [[ "$(printf '%s\n3.11\n' "${version}" | sort -V | head -1)" != "3.11" ]]; then
        echo "python3 ${version} found; 3.11 or later is required (tomllib)" >&2
        exit 1
    fi
    echo "python3 ${version}: ok"
}

check_clock() {
    if command -v timedatectl >/dev/null 2>&1; then
        if [[ "$(timedatectl show --property=NTPSynchronized --value)" != "yes" ]]; then
            echo "WARNING: host clock is not NTP-synchronised." >&2
            echo "         Install chrony and re-check before trusting any timing data." >&2
        else
            echo "clock synchronised: ok"
        fi
    fi
}

require_root
check_python
check_clock

id -u "${SERVICE_USER}" >/dev/null 2>&1 || useradd --system --home "${DATA_DIR}" --shell /usr/sbin/nologin "${SERVICE_USER}"

install -d -o "${SERVICE_USER}" -g "${SERVICE_USER}" "${DATA_DIR}"
install -d "${PREFIX}"
cp -r "${SOURCE_DIR}/src" "${SOURCE_DIR}/config" "${PREFIX}/"
install -d "${PREFIX}/docs"
cp -r "${SOURCE_DIR}/../docs/"*MODEL* "${PREFIX}/docs/" 2>/dev/null || true

echo "running safety interlock..."
PYTHONPATH="${PREFIX}/src" python3 -m velocita_model.cli verify

install -m 0644 "${SOURCE_DIR}/systemd/velocita-model-collector.service" /etc/systemd/system/
install -m 0644 "${SOURCE_DIR}/systemd/velocita-model-report.service" /etc/systemd/system/
install -m 0644 "${SOURCE_DIR}/systemd/velocita-model-report.timer" /etc/systemd/system/
systemctl daemon-reload

cat <<EOF

installed to ${PREFIX}, data in ${DATA_DIR}

Next, in order:
  1. Confirm every field listed in docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md.
  2. Copy config/research.example.toml to ${PREFIX}/config/research.toml and
     fill in the confirmed values. Pairs stay disabled until you do.
  3. python3 -m velocita_model.cli config ${PREFIX}/config/research.toml
  4. systemctl enable --now velocita-model-collector.service
  5. systemctl enable --now velocita-model-report.timer

Nothing collects until a pair is explicitly enabled, and nothing trades ever.
EOF
