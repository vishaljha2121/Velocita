#!/usr/bin/env bash
set -euo pipefail

if ! dpkg-query -W prometheus-process-exporter >/dev/null 2>&1; then
    echo "Install prometheus-process-exporter before running this script." >&2
    exit 1
fi

sudo install -d -m 0755 \
    /etc/velocita \
    /etc/systemd/system/prometheus-process-exporter.service.d
sudo install -m 0644 \
    deploy/observability/process-exporter.yml \
    /etc/velocita/process-exporter.yml
sudo install -m 0644 \
    deploy/systemd-system/prometheus-process-exporter.service.d/velocita.conf \
    /etc/systemd/system/prometheus-process-exporter.service.d/velocita.conf

sudo systemctl daemon-reload
sudo systemd-analyze verify prometheus-process-exporter.service
sudo systemctl enable prometheus-process-exporter.service
sudo systemctl restart prometheus-process-exporter.service

ready=0
for attempt in {1..20}; do
    if curl --fail --silent \
        http://127.0.0.1:9256/metrics >/dev/null; then
        ready=1
        break
    fi
    sleep 1
done

if [[ "$ready" -ne 1 ]]; then
    echo "process exporter did not become ready within 20 seconds" >&2
    sudo systemctl --no-pager --full status \
        prometheus-process-exporter.service >&2 || true
    exit 1
fi

echo "Installed the bounded process-exporter configuration."
echo "Endpoint: http://127.0.0.1:9256/metrics"
