# Fresh deadlines after blocking ports

The initial native checkpoint passed all 59 CTest targets on both architectures, then failed the packaging target allowlist. After that isolated check was corrected at `887d6f5`, NUC1 fully qualified, but the Pi's next run stopped in continuous case 0 after signature 4 and before execution 4 (`failed/state`, trace6). That record is retained under `attempt-887d6f5/pi/`; it is not a successful qualification.

Review found a concrete stale-time bug: `SwapGatewayRuntime::observed()` could synchronously wait for signing or finalized receipt lookup, then call `command(..., now, event)` with the timestamp from before the wait. `GatewayLink::start()` sets its deadline from that old time; `pump()` checks the deadline before its first send. Thus a wait longer than the link timeout can cause the next Raft command to expire unsent. Old event time can also backdate an Attempt after authorization expires. The old native transcript is consistent with this defect but lacks timing evidence to establish it as the exclusive cause of that intermittent failure.

## Correction

- Hold the next `swap::Command` in one bounded, owning `std::optional` slot. Submit it at the next loop tick using fresh monotonic and event times. Assign sequence and digest only then.
- Check storage, monotonic clocks and absence of an active link/session before consuming the slot; stopping clears it. Kernel expiry and economic checks remain unchanged.
- When retrieving signing for an already-attempted effect, yield and read current state before starting the submission session; that path does not pass through the command slot.
- Keep all existing timeout values. No Raft schema, journal format, economic cap or credential changes. A crash before a queued command is sent retains the existing durable preparation/signing evidence and existing supervised recovery limitations.
- Add test-only signing/step duration and last Attempt-offered diagnostics, without production logging or key/payload output.

## Deterministic evidence

Runtime modes 11/12/13 advance the supplied test clock by exactly `timeout+1` during signing, receipt lookup and recovered-signature retrieval. They do not sleep or rely on slow hardware. Mode14 expires authorization during signing and requires that the next Attempt cannot use the old event timestamp. All four assertions fail against the previous source and pass with the correction. The complete 15-scenario runtime target and three six-effect continuous cases pass locally; logs are adjacent.

The next native qualification has a fixed three repetitions of the complete continuous test (nine scenarios, 54 fixture trades per architecture), in addition to the selected 59-test suite. Every failure stops qualification; no retry-until-pass. This repetition count is declared before running the corrected native checkpoint.

This is not a general conversion of the coordinator into asynchronous I/O. Synchronous storage/scheduling can still consume operation time and cause a safe stop. Native success does not prove real-venue timing or unattended recovery from every prepared-state crash.
