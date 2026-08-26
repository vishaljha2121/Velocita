#!/usr/bin/env bash
set -euo pipefail

prometheus_image="docker.io/prom/prometheus:v3.13.2"
grafana_image="docker.io/grafana/grafana:13.1.3"
benchmark_image="${VELOCITA_BENCHMARK_IMAGE:-localhost/velocita-raft:bench}"
config_root="${XDG_CONFIG_HOME:-$HOME/.config}/velocita/observability"
quadlet_dir="${XDG_CONFIG_HOME:-$HOME/.config}/containers/systemd"
user_unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"

if ! podman image exists "$benchmark_image"; then
    echo "Benchmark image is required for its isolated metrics exporter: $benchmark_image" >&2
    echo "Run ./deploy/build-benchmark-image.sh first." >&2
    exit 1
fi

if ! curl --fail --silent --show-error http://127.0.0.1:9100/metrics >/dev/null; then
    echo "node exporter is not available on 127.0.0.1:9100" >&2
    exit 1
fi

if ! curl --fail --silent --show-error http://127.0.0.1:9256/metrics >/dev/null; then
    echo "process exporter is not available on 127.0.0.1:9256" >&2
    exit 1
fi

echo "Pulling pinned ARM64-capable observability images..."
podman pull "$prometheus_image"
podman pull "$grafana_image"

podman run --rm \
    --network none \
    --pull never \
    --entrypoint /bin/promtool \
    --volume "$PWD/deploy/observability:/etc/prometheus:ro" \
    "$prometheus_image" \
    check config /etc/prometheus/prometheus.yml

install -d -m 0755 \
    "${XDG_DATA_HOME:-$HOME/.local/share}/velocita/benchmarks" \
    "$config_root/prometheus/rules" \
    "$config_root/grafana/provisioning/datasources" \
    "$config_root/grafana/provisioning/dashboards" \
    "$config_root/grafana/dashboards" \
    "$quadlet_dir" \
    "$user_unit_dir"

latest_benchmark_metrics="${XDG_DATA_HOME:-$HOME/.local/share}/velocita/benchmarks/latest.prom"
if [[ ! -e "$latest_benchmark_metrics" ]]; then
    install -m 0644 /dev/null "$latest_benchmark_metrics"
fi

install -m 0644 deploy/observability/prometheus.yml \
    "$config_root/prometheus/prometheus.yml"
install -m 0644 deploy/observability/rules/*.yml \
    "$config_root/prometheus/rules/"
install -m 0644 deploy/observability/grafana/provisioning/datasources/*.yml \
    "$config_root/grafana/provisioning/datasources/"
install -m 0644 deploy/observability/grafana/provisioning/dashboards/*.yml \
    "$config_root/grafana/provisioning/dashboards/"
install -m 0644 deploy/observability/grafana/dashboards/*.json \
    "$config_root/grafana/dashboards/"

install -m 0644 \
    deploy/quadlet/velocita-prometheus.container \
    deploy/quadlet/velocita-grafana.container \
    deploy/quadlet/velocita-benchmark-metrics-exporter.container \
    deploy/quadlet/velocita-prometheus-data.volume \
    deploy/quadlet/velocita-grafana-data.volume \
    "$quadlet_dir/"
install -m 0644 deploy/systemd-user/velocita-observability.target \
    "$user_unit_dir/"

systemctl --user daemon-reload
systemd-analyze --user --generators=true verify \
    velocita-prometheus.service \
    velocita-grafana.service \
    velocita-benchmark-metrics-exporter.service
systemctl --user enable velocita-observability.target
# Restart the target so an already-active installation also starts newly added
# dependencies and reloads Prometheus/Grafana provisioning.
systemctl --user restart velocita-observability.target

echo "Installed and started the bounded local observability stack."
echo "Prometheus: http://127.0.0.1:9090"
echo "Grafana:    http://127.0.0.1:3000"
echo "For tailnet access, publish Grafana with Tailscale Serve; do not bind port 3000 publicly."
