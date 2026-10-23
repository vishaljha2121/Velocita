# Completed command record; do not rerun as a script

No secrets are included. The current ledger and journals already exist. These
commands describe the completed operation; initial funding/initialization must
not be repeated.

1. Ran the existing authenticated_read_run.py from the owner account using sudo,
   the pinned helper/catalog/incarnation/authority arguments in cluster-read.json,
   and a new funding-pre-read output. Both callers returned index4/state_version3.
2. Used `podman ps --format '{{.Names}} {{.Status}}'` over SSH to each host.
   Exactly one existing voter container per host. Checked the Mac endpoint with
   `ifconfig`; Tailscale's app CLI could not load its preferences.
3. Ran tm-account-history-check.py into funding-start-prehistory-20261013 and
   read_route.py once. Read-only, no signing or execution. Then the owner approved
   the scoped ceremony checkpoint; committed d63f93d with explicit paths only.
4. Staged committed code with:

       git archive d63f93d deploy/v2/swap-runtime tests/test_swap_review_transition.py tests/test_swap_account_adoption.py tests/test_swap_service_launcher.py tests/test_swap_service_profile.py | ssh vishal@100.72.127.102 'umask 077; mkdir "$HOME/.cache/velocita-build/funding-start-d63f93d-20261013" && tar -xf - -C "$HOME/.cache/velocita-build/funding-start-d63f93d-20261013" && cd "$HOME/.cache/velocita-build/funding-start-d63f93d-20261013" && python3 -B -m unittest discover -s tests -p test_swap_review_transition.py'

5. Obtained one `tm-mvp-quote-check.py --side sell --run` unsigned quote using the
   fresh history hash. Captured final account history immediately afterwards.
   Called prepare_documents.py with --route pointing to the new six-account
   capture and the old unconfigured manifest as an offline template only.
   prepare_transitions.py then bound the *actual installed* previous manifest
   and documents to the new documents; the initial-adoption bundle was not used.
6. Ran owner-role.zsh --run from the Mac. Its exact key-stdin command and owner
   sequence4 are retained in that file; result at index5 in ceremony.log.
7. Transferred only public account/profile transition documents over SSH/tar into
   the owner cache. Verified review_transition.pySHA
   085150bf936bd5b501799fd751c8ff3f6135fbe61ccccef95a8b7eca2d383c4e.
8. Owner sudo invoked the committed helper, using the following form:

       python3 -B "$HOME/.cache/velocita-build/funding-start-d63f93d-20261013/deploy/v2/swap-runtime/review_transition.py" --bundle "$HOME/.cache/velocita-build/funding-start-d63f93d-20261013/account/transitions/refresh" --sha256 9548fffc7aa2e7a747abbe0efca40a0c754f72c5b6667aacb504eefcf69fabeb --apply

   This archived and replaced the expired dormant documents, verified existing
   credentials, and reloaded systemd. It did not start anything.
9. Mac: owner-funding.zsh --dry-run, inspect summary/hash, then --run once.
   Owner request5 committed the reviewed Initialize at index6. Trading halted.
10. Owner sudo invoked review_transition.py for the `arm` bundle with SHA
    e9693c6f8b424fc632e107c115800813fe7d3ba42326072af47eae0f23467043.
    After success, executed `sudo systemctl start velocita-swap-signer.service`.
    Inspected active state, log, socket and journal before proceeding.
11. Executed `sudo systemctl start velocita-swap-gateway.service` once. Inspected
    active state, logs and all four journal files before proceeding.
12. Invoked review_transition.py for the `seal` bundle with SHA
    9b0c12f4cf772dea6834f2437f6f157b7cf7715ba6d618efa8a7e00413555b1d.
    It removed initialization from future ExecStart arguments and daemon-reloaded;
    neither service restarted. Final manifest hash is in startup-verified.json.
13. Read-only root checks verified both actual executable hashes, UIDs, socket,
    checksummed idle journals, ordinal1/effect0/index6, zero restarts and the
    final manifest. Read exporter endpoints on all three hosts; all up/halted.
14. Saved sanitized verification reports through scp. Ran `sudo -k && exit`.

Safe current status/log commands (do not restart):

    ssh vishal@100.72.127.102 'systemctl show --no-pager velocita-swap-signer.service velocita-swap-gateway.service -p Id -p ActiveState -p SubState -p ExecMainStatus'
    ssh -t vishal@100.72.127.102 'sudo journalctl -f -u velocita-swap-signer.service -u velocita-swap-gateway.service'

The review expires22:17:27UTC; subsequent stopped status is expected. Preserve
journals and refresh against the existing opening before any later launch.
