# Alert delivery repair — 2026-10-07

Owner approved refreshing ntfy's mount, correcting Alertmanager storage ownership,
and verifying delivery. Work was limited to the two alert services on nuc1.

## Verified causes

- ntfy's running `/etc/ntfy/templates` mount was empty although the source
  directory contained `velocita.yml`. Webhooks failed with HTTP 400/code 40047,
  `invalid request: template file not found`.
- Alertmanager runs as container UID/GID 0:0 with all capabilities dropped.
  Its empty data volume directory belonged to container UID/GID 65534:65534,
  mode 775. Saving notification history and silences failed with permission denied.

## Commands and changes

Run as `velocitas@nuc1`. Before changing ownership, saved current silence API
output, delivery counters and ownership under `~/alert-delivery-repair-20261007/`.
Verified the volume path matched the expected rootless Podman data volume and
that the directory was empty after stopping Alertmanager.

```sh
volume="$(podman volume inspect velocita-v2-alertmanager-data --format '{{.Mountpoint}}')"
systemctl --user stop velocita-v2-alertmanager.service
podman unshare chown 0:0 "$volume"
systemctl --user restart velocita-ntfy.service
# After the container starts (systemctl restart returns before it is ready):
podman exec velocita-ntfy sha256sum /etc/ntfy/templates/velocita.yml
systemctl --user start velocita-v2-alertmanager.service
```

Ownership changed on that directory only, without recursive changes, deletion,
credential rotation, or permission broadening. Template SHA256 inside ntfy:
`04d3df939c295b1051a23bb72fab6bbe8010cd3c6ca6bd36a73064767de01460`.
The first immediate container inspection raced startup; the follow-up confirmed
the mount before starting Alertmanager. No service restart loop occurred.

## Delivery and persistence evidence

- At 22:01:23 UTC submitted one warning test through local Alertmanager's
  `/api/v2/alerts`, receiving HTTP 200. Name `VelocitaDeliveryRepairTest`,
  test_id `20261007T220123Z`; expiry 22:03:23 UTC.
- At 22:01:53 UTC ntfy stored the rendered test on `velocita-alerts`, published=1,
  title `VelocitaDeliveryRepairTest`, body `Delivery repair test - no trading incident`.
  Verified via a read-only query of the message cache, without reading credentials.
- Before the persistence verification restart: three webhook deliveries,
  zero failed deliveries across all failure reasons.
- Gracefully stopped Alertmanager again to verify real persistence: `nflog`
  saved (317 bytes), `silences` saved (empty, matching no silences), ownership 0:0.
  Restarted successfully at 22:02:15 UTC; readiness endpoint returned OK.
- ntfy and Alertmanager active, zero automatic restarts. Paper edge stayed active,
  zero automatic restarts; voters, gateway, ledger and trading configuration untouched.

Raw evidence remains on nuc1 under `~/alert-delivery-repair-20261007/`.
Phone display/receipt requires owner confirmation; successful server publication
does not prove iOS delivery. The delivery-failure alert uses a 15-minute lookback,
so earlier failures can keep its warning visible until that window expires.

Future source-directory swaps must also refresh ntfy's bind mounts. This repair
does not alter the source-shipping automation or change the service images.
