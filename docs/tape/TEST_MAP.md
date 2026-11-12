# Tape test map

Every test case in ADR 0030 mapped to the tests that exist on 2026-11-12, plus the guarantees added since. **Covered**: a test checks what the case describes. **Partly**: a test checks some of it or at a smaller scale; what is missing is stated. **Not covered**: no test yet (most were deferred in IMPLEMENTATION_PLAN.md; some were checked only by hand in live runs).

Of 92 cases: 49 covered, 22 partly, 21 not covered.

Python tests live in `tests/test_tape_core.py`, `test_tape_recorder.py`, `test_tape_end_to_end.py` and `test_tape_report.py`; C++ tests in `tests/tape_replica_tests.cpp`, `tape_voter_tests.cpp` and `tape_submitter_tests.cpp`. `tape-core-tests` runs `test_tape_core.py`.

## X

| Case | What it does | Status | Tests |
|---|---|---|---|
| X-1.1 | SYMCHECK every tape target against a denylist: venue order entry, request signing, signer protocol, swap runtime | Covered | ctest `tape-never-trades-velocita-tape-{voter,recorder,verify,keys}` |
| X-1.2 | Scan the tape's units and container definitions for mounted key files and venue credential variables | Not covered | deferred in the plan |
| X-2.1 | Send a correctly signed trading-cluster command to a tape voter | Partly | `tape_voter_tests: test_refusals` (a frame sealed with another key is rejected); no real trading-cluster command is sent |
| X-2.2 | Send a tape observation to a trading voter | Not covered | deferred: needs a trading voter |
| X-2.3 | Derive tape and trading keys from one master key | Not covered | deferred: key labels differ by design, untested |
| X-3.1 | Plant a marker string in F-WS frames; scrape the exporter and read the logs at info level | Not covered | deferred for the exporter and logs; the report has its own check (`test_no_payload_bytes_leave_the_report`) |

## REC

| Case | What it does | Status | Tests |
|---|---|---|---|
| REC-1.1 | Play frames with odd whitespace, trailing zeros (`"182.4000"`) and Unicode | Covered | `test_tape_recorder: test_bytes_and_hashes_are_exactly_what_was_sent` |
| REC-1.2 | Hash every committed observation | Covered | `test_bytes_and_hashes_are_exactly_what_was_sent` |
| REC-1.3 | Send one message as fragmented WebSocket frames | Not covered | the client handles continuation frames; no test sends one |
| REC-2.1 | Send `seqnum` 10 twice | Covered | `test_duplicates_reordering_and_malformed_frames_are_all_kept` |
| REC-2.2 | Send 10, 12, 11 | Covered | `test_duplicates_reordering_and_malformed_frames_are_all_kept` |
| REC-2.3 | Send a malformed frame | Covered | `test_duplicates_reordering_and_malformed_frames_are_all_kept` |
| REC-2.4 | Send 10,000 frames | Not covered | no 10,000-frame run |
| REC-3.1 | Drop `seqnum` 90415 | Covered | `test_gap_is_recorded_and_only_that_stream_resubscribes`, `test_resync_waits_for_each_reply_and_the_stream_ends_up_subscribed` |
| REC-3.2 | Send a lower or repeated `seqnum` | Covered | `test_regression_is_recorded_without_resubscribing` |
| REC-3.3 | Gap on one stream while other streams flow | Covered | `test_gap_is_recorded_and_only_that_stream_resubscribes` |
| REC-4.1 | F-WS goes silent | Covered | `test_silence_ends_the_connection_and_heartbeats_keep_it` |
| REC-4.2 | F-WS sends only heartbeats for 10 minutes | Partly | `test_silence_ends_the_connection_and_heartbeats_keep_it` (seconds, not 10 minutes) |
| REC-4.3 | Inspect every subscription request | Covered | `test_every_subscription_asks_for_heartbeats_and_only_no_login_requests_are_sent` |
| REC-4.4 | F-WS refuses every connection | Not covered | deferred in the plan |
| REC-5.1 | Cluster refuses everything; send ring capacity + 100 frames, then resume | Covered | `test_full_ring_records_one_overflow_with_the_exact_range` |
| REC-5.2 | Hold the ring full while F-WS keeps sending | Not covered | deferred in the plan |
| REC-6.1 | Record every frame the recorder sends | Covered | `test_every_subscription_asks_for_heartbeats_and_only_no_login_requests_are_sent` |
| REC-6.2 | Connect storm against a refusing F-WS | Not covered | deferred in the plan |
| REC-7.1 | Step UTC back 2 s mid-run | Partly | `test_receive_times_are_monotonic_and_utc_is_recorded` (monotonic times recorded; the clock is not stepped back) |
| REC-7.2 | Remove the fake chrony source | Not covered | deferred in the plan |

## ING

| Case | What it does | Status | Tests |
|---|---|---|---|
| ING-1.1 | Recorder 1's key with recorder id 2 inside | Covered | `tape_voter_tests: test_refusals` (a recorder impersonating another is refused by every voter) |
| ING-1.2 | Wrong key | Covered | `tape_voter_tests: test_refusals` (a frame sealed with the wrong key is rejected) |
| ING-1.3 | An expired session | Partly | `tape_voter_tests: test_refusals` (a forged session never reaches the log); expiry itself is not tested |
| ING-1.4 | A caller whose role is not recorder | Partly | `tape_voter_tests: test_refusals` (a recorder's tick is refused); other roles not tested |
| ING-2.1 | Resend the same request sequence after a forced election | Partly | `test_tape_core: test_duplicate_and_out_of_order_requests_are_refused`, `tape_voter_tests: test_ordering_acks_ticks_and_heads` (exactly once, in order); no forced election |
| ING-2.2 | Replay a captured frame after a restart | Not covered | deferred in the plan |
| ING-3.1 | 10,000 observations with 20% not-leader, 10% queue-full and 5% timeouts | Not covered | deferred in the plan |
| ING-3.2 | A timeout after the entry committed, then a retry | Partly | `test_duplicate_and_out_of_order_requests_are_refused` (a retry of a committed request is refused) |
| ING-4.1 | A snapshot frame three times the entry limit | Covered | `test_bytes_and_hashes_are_exactly_what_was_sent` (9 KB frame split), `test_tape_core: test_split_observation_matches_a_whole_one` |
| ING-4.2 | Drop one part | Partly | `test_split_observation_matches_a_whole_one` (a corrupted part is refused); a dropped part is not tested |
| ING-4.3 | Submit one oversize entry directly | Not covered | no test submits an oversize entry directly |

## RAFT

| Case | What it does | Status | Tests |
|---|---|---|---|
| RAFT-1.1 | Run the existing durable-quorum scenarios against the tape group's configuration | Partly | `tape_replica_tests: test_no_majority_commits_nothing`, `test_one_voter_down_keeps_committing_and_rejoins`; the trading suite's durable-quorum scenarios are not run against the tape |
| RAFT-1.2 | Configure with an unpatched Raft source | Not covered | deferred in the plan |
| RAFT-2.1 | 50,000 entries under a seeded random restart schedule | Partly | `tape_replica_tests: test_random_restarts_keep_history_identical` (hundreds of entries, not 50,000) |
| RAFT-3.1 | Corrupt each field of a WAL block, a snapshot and an archive segment in turn | Partly | `test_wal_recovery_trims_torn_tail_and_refuses_corruption` (WAL), `test_tape_report: test_a_damaged_log_is_excluded_and_others_still_count` (archive segment); not every field |
| RAFT-4.1 | Stop one voter; keep recording; restart it | Covered | `tape_replica_tests: test_one_voter_down_keeps_committing_and_rejoins`; also the live unplug drills (D-41) |
| RAFT-5.1 | Random logs, snapshot at a random index, restore and replay the rest | Not covered | superseded: compaction was replaced by 4-hour segments (D-55, D-58) |

## ATT

| Case | What it does | Status | Tests |
|---|---|---|---|
| ATT-1.1 | 1,000 messages with random arrival orders, missing copies, duplicates and divergence | Covered | `test_tape_core: test_random_arrivals_match_reference` |
| ATT-1.2 | One recorder sends the same copy twice | Covered | `test_a_recorders_second_copy_is_ignored` |
| ATT-2.1 | All three report within two ticks | Covered | `test_all_three_close_at_the_third_copy` |
| ATT-2.2 | One recorder silent | Covered | `test_silent_recorder_closes_at_exactly_n_ticks` |
| ATT-2.3 | The third copy commits just before, then just after, the Nth tick | Covered | `test_third_copy_on_either_side_of_the_close_tick` |
| ATT-3.1 | A matching copy after close | Partly | `test_late_divergent_copy_makes_the_book_unknown_without_reopening` covers late copies; a late matching copy is only counted, not asserted on its own |
| ATT-3.2 | A divergent copy after close | Covered | `test_late_divergent_copy_makes_the_book_unknown_without_reopening` |
| ATT-4.1 | Three recorders send three different versions | Covered | `test_three_versions_are_divergent_not_a_majority` |
| ATT-4.2 | Two recorders agree, one differs | Covered | `test_three_versions_are_divergent_not_a_majority`, `test_tape_report: test_divergent_copies_are_counted` |
| ATT-5.1 | Open verdicts past capacity, with ticks held | Covered | `test_capacity_closes_the_oldest` |
| ATT-6.1 | SYMCHECK the reducer library | Covered | ctest `tape-reducer-is-pure` |

## TICK

| Case | What it does | Status | Tests |
|---|---|---|---|
| TICK-1.1 | A forced election where the old and new leaders both propose tick k+1 | Partly | `test_ticks_must_be_consecutive` (a repeated tick is refused); no forced election |
| TICK-1.2 | Tick k+3 right after k | Covered | `test_ticks_must_be_consecutive` |
| TICK-2.1 | A tick signed with a recorder key, and one with a voter key from another incarnation | Partly | `tape_voter_tests: test_refusals` (a recorder's tick is refused); a tick from another incarnation is not tested |
| TICK-3.1 | Change every tick's UTC field and replay | Covered | `test_tick_time_is_never_used` |
| TICK-3.2 | Run the verifier on a log with a burst of 0.2 s ticks and a 12 s election gap | Not covered | the verifier flags tick spacing in live runs (D-46); no unit test |

## BOOK

| Case | What it does | Status | Tests |
|---|---|---|---|
| BOOK-1.1 | Replay synthetic logs and, when available, day-one captures | Covered | `test_random_arrivals_match_reference`, `test_two_real_connections_replay_identically_to_reference` |
| BOOK-1.2 | Replay one archive on a NUC, the Pi and a Mac (`tape-drills`) | Partly | live: identical roots and signed heads on both NUCs and the Pi (D-41, D-46, D-59); no automated drill |
| BOOK-2.1 | Parse `"182.4000"` at scale 4 | Covered | `test_decimal_boundaries` |
| BOOK-2.2 | Parse `"182.40001"` at scale 4 | Covered | `test_decimal_boundaries` |
| BOOK-2.3 | Parse values at and past the integer limit | Covered | `test_decimal_boundaries` |
| BOOK-2.4 | Parse `"-1"`, `"1e3"`, `""` and `"NaN"` | Covered | `test_decimal_boundaries` |
| BOOK-3.1 | Three identical copies of one update | Covered | `test_all_three_close_at_the_third_copy`, `test_a_recorders_second_copy_is_ignored` |
| BOOK-4.1 | A divergent copy at index i | Covered | `test_late_divergent_copy_makes_the_book_unknown_without_reopening`, `test_three_versions_are_divergent_not_a_majority` |
| BOOK-4.2 | Divergence on one symbol | Partly | `test_three_versions_are_divergent_not_a_majority` (one symbol); other symbols staying valid is not asserted |
| BOOK-5.1 | `seqnum` s+1 commits before s, and s arrives within N ticks | Covered | `test_held_message_applies_in_order_when_it_arrives_in_time` |
| BOOK-5.2 | s never arrives | Covered | `test_missing_message_makes_the_book_unknown_after_n_ticks` |
| BOOK-5.3 | The reorder buffer fills before N ticks | Covered | `test_reorder_buffer_full_is_unknown_at_once` |
| BOOK-6.1 | A lagging recorder's snapshot behind the book's position | Covered | `test_stale_snapshot_is_ignored_and_interleaved_snapshot_applies_at_complete` |
| BOOK-6.2 | A resyncing recorder's snapshot interleaved with other recorders' updates | Covered | `test_stale_snapshot_is_ignored_and_interleaved_snapshot_applies_at_complete`, `test_snapshots_from_different_subscriptions_are_not_copies`, `test_a_recorders_own_resubscription_snapshot_is_not_a_copy` |
| BOOK-7.1 | Walk every state transition | Partly | every transition is exercised across the BOOK tests; no single walk |
| BOOK-8.1 | Flood one book with ten times the level capacity | Covered | `test_level_capacity_makes_the_book_unknown`, `test_snapshot_at_exactly_capacity_is_valid`, `test_capacity_while_draining_leaves_no_hash` |
| BOOK-8.2 | Subscribe more symbols than capacity | Not covered | deferred in the plan |
| BOOK-9.1 | Replay with the integrity checks built in and built out | Covered | `test_integrity_checks_never_change_books` |
| BOOK-9.2 | Fixtures for each rule: bid above ask, bid equal to ask, EBBO disagreeing for 2 and for 3 ticks, a trade outside the book, a trade outside the band, a | Partly | `test_snapshot_still_wrong_after_the_next_update_is_flagged`, `test_tape_report: test_inconsistent_books_are_flagged` (crossed, trade outside the book); EBBO and band rules not tested |

## PROOF

| Case | What it does | Status | Tests |
|---|---|---|---|
| PROOF-1.1 | Leaf and node hashes for trees of size 1 to 8 | Covered | `test_rfc_vectors`, `test_cpp_merkle_matches_rfc_vectors` |
| PROOF-1.2 | A long run; check every consecutive pair of tree heads | Covered | `test_tape_end_to_end: test_every_voter_verifies_and_agrees` (the verifier checks consistency between consecutive heads) |
| PROOF-2.1 | Compare the three voters' head bytes at every publication index | Partly | `test_every_voter_verifies_and_agrees` (identical roots); head bytes not compared at every index |
| PROOF-3.1 | Heads with one signature, a wrong key, and a signature over different bytes | Partly | `test_tape_report: test_a_head_with_one_signature_is_not_enough`, `tape_voter_tests` (heads carry two valid signatures); wrong key and wrong bytes not tested |
| PROOF-4.1 | Inclusion proofs for 1,000 random events against their first covering head | Covered | `test_cluster_root_equals_reference_root_and_proofs_verify` |
| PROOF-4.2 | Proof sizes for trees of 1,000, 1,000,000 and 10,000,000 entries | Covered | `test_proof_sizes_by_hand` |

## MET

| Case | What it does | Status | Tests |
|---|---|---|---|
| MET-1.1 | SYMCHECK the exporter | Covered | ctest `tape-exporter-links-no-tape-code` |
| MET-2.1 | Record a fixture day; the verifier recomputes every `source="log"` metric | Not covered | not built |
| MET-2.2 | Parse a full scrape | Not covered | not built |
| MET-3.1 | Error bound 2 ms; true skew 1 ms, then 5 ms | Not covered | deferred in the plan |
| MET-4.1 | Kill, then pause, the exporter during a deterministic recording run | Not covered | deferred in the plan |

## VER

| Case | What it does | Status | Tests |
|---|---|---|---|
| VER-1.1 | Run with networking disabled (`unshare -n` on Linux, a sandbox profile on macOS) | Covered | `test_tape_end_to_end: test_verifier_needs_no_network` |
| VER-2.1 | Corrupt, in turn, one entry byte, one book hash, one inclusion proof, one consistency proof, the tick order, one verdict and one metric | Partly | `test_tampering_that_repairs_its_checksum_is_caught_and_located` (entry tampering); the other injected faults are not each tested |
| VER-3.1 | For each injected fault | Partly | `test_tampering_that_repairs_its_checksum_is_caught_and_located` (location reported for that fault) |

## Added after the ADR

| Guarantee | What it covers | Tests |
|---|---|---|
| SEG-1 link | Each segment starts with one `segment_start` naming the previous signed head | `test_tape_core: test_a_segment_records_its_predecessor_once`; `test_tape_report: test_link_to_the_wrong_head_excludes_the_segment`; local rotation run (D-55) |
| SEG-2 incarnation | One clock-derived incarnation per boundary on every host | `test_tape_rotate` (all six tests, plus `test_a_legacy_random_incarnation_rotates_into_a_time_segment`) |
| SEG-3 final coverage | Recorder stops before the voter | `test_tape_rotate: test_a_segment_is_archived_and_the_next_one_links_to_its_last_head` (stop and start order) |
| SEG-4 archive | NUC2 keeps every segment | `test_tape_rotate: test_keep_zero_keeps_every_segment`, `test_only_the_newest_segments_are_kept_locally` |
| Network ceilings (D-42, D-43) | Each entry sent once; byte budgets; backoff | `tape_replica_tests: test_slow_links_send_each_entry_about_once`, `test_lossy_links_still_commit_everything`, `test_a_follower_that_never_answers_costs_little`, `test_catch_up_respects_the_byte_budget`; `tape_submitter_tests` (all three) |
| Watchdog (D-43, D-50) | Stops a flood, restarts after quiet | `test_tape_watchdog` (six tests) |
| Recorder control requests (D-47) | One request at a time; retry rejections | `test_resync_waits_for_each_reply_and_the_stream_ends_up_subscribed`, `test_a_rejected_request_is_retried_until_the_server_accepts_it` |
| Signed heads gain signatures (D-40) | Late third signature published | `tape_voter_tests` (a published head gains the third signature once it lands) |
| Report RPT-1 to RPT-8 | Feed-quality report | `test_tape_report` (16 tests), mapped case by case in REPORT_PLAN.md |
