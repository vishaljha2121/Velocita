# Signer accept wait must not consume the request deadline

At `26b1e51`, NUC1 completed the selected suite and all three fixed continuous repetitions.
Pi stopped in continuous case 0 after signature 2, before Attempt/execute 2: gateway
`blocked/signer_refused`, last signing call 618 ms, last Attempt offered 1. Both transcripts
are retained under `attempt-26b1e51/`. This is not dual-architecture qualification.

`Server::serve_once` started its deadline before waiting for a connection, then reused it for
request input, independent authorization, durable signing and reply output. A late client had
only the remainder of the idle wait's budget. Signing could complete and be saved, then the
reply write would fail because its deadline had passed. This is a separately demonstrated
bug; the Pi trace is consistent with it but does not directly measure accept time.

## Minimal correction and oracle

Keep the bounded accept wait. Immediately after accepting a connection, start a fresh fixed
request deadline with the same configured timeout. Do not reset it per byte, poll or signing
phase. No key, UID check, request digest, signature verification, journal, authorization,
economic constraint, replay fence or client timeout is changed.

The new socket regression connects 1,250 ms into a 2,000 ms accept window, sends the frame
header, then waits another 1,100 ms before sending the body. The body arrives beyond the old
accept deadline but within the accepted connection's budget. It must produce exactly one
signature, and the client verifies the response digest, public key and signature bytes.
The old code fails that assertion (retained RED log). This integration test uses real elapsed
time with substantial margins; unlike the runtime clock regressions it is not a simulated-clock
proof. Its readiness signal immediately precedes the accept call; extreme descheduling can
blur the wall-clock timing assumption. A companion incomplete-frame test requires timeout, socket closure and zero signatures.
An initial sandbox socket-bind refusal was an environment failure, not counted as RED.

## Operational bound

A call now has one accept-timeout budget plus one request-timeout budget. These are fixed
deadlines, not a strict wall-clock bound on synchronous crypto/storage or scheduler delays.
The client still has its original end-to-end deadline. Existing systemd stop
timeouts may force a process down before a long request finishes; durable signing/submission
fences remain the recovery authority. This change does not promise graceful completion during
shutdown, automatic retries, or a universally asynchronous signer. The native protocol remains
59 selected CTest targets and exactly three continuous-suite repetitions per architecture,
stopping on the first failure. The next run must use a newly committed source checkpoint.
