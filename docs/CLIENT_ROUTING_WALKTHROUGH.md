# Client routing walkthrough

This document explains how the Raft client finds the current leader and sends a
command to it. The relevant files are:

- [`include/velocita/client_routing.hpp`](../include/velocita/client_routing.hpp)
- [`src/client_routing.cpp`](../src/client_routing.cpp)
- [`src/client_main.cpp`](../src/client_main.cpp)
- [`src/client_session.cpp`](../src/client_session.cpp)

## The problem the router solves

A Raft cluster has several nodes, but only the current leader may accept a new
command. The client may not know which node is leader when it starts. Leadership
can also change while the client is running.

The `LeaderRouter` answers one question:

> Which Raft node should receive the next copy of this command?

The router does not send network messages. It only remembers what the client has
learned about the leader and chooses a target. `AeronClientSession` performs the
actual Aeron network communication.

For the three-node Pi cluster, the configured targets are:

```text
node 1 -> aeron:udp?endpoint=127.0.0.1:40111
node 2 -> aeron:udp?endpoint=127.0.0.1:40112
node 3 -> aeron:udp?endpoint=127.0.0.1:40113
```

## The routing data types

### `RequestTarget`

```cpp
struct RequestTarget {
    std::int32_t node_id{};
    std::string channel;
};
```

A `RequestTarget` connects a Raft node ID to the Aeron channel used to send
client requests to that node.

For example:

```cpp
RequestTarget{
    .node_id = 2,
    .channel = "aeron:udp?endpoint=127.0.0.1:40112"
};
```

### `LeaderKnowledgeState`

The router keeps one of three leader-knowledge states:

```text
unknown  -> The client does not currently know the leader.
suspect  -> Another node suggested a leader, but that leader has not replied successfully yet.
known    -> The suggested leader directly returned a successful response.
```

The normal progression is:

```text
unknown
   |
   | follower says "node 2 is the leader"
   v
suspect
   |
   | node 2 successfully handles the command
   v
known
```

### `LeaderHintResult`

`LeaderHintResult` describes what happened during one call to `observe()`:

```text
confirmed -> A successful response directly confirmed the leader.
updated   -> A response supplied a usable but indirect leader hint.
unknown   -> The responding node does not currently know the leader.
stale     -> The response belongs to an older Raft term.
invalid   -> The response contains impossible or malformed leader information.
```

`LeaderHintResult` describes one event. `LeaderKnowledgeState` describes what
the router currently believes after processing events.

## Startup: parsing the configuration

The cluster client supplies the membership and request targets:

```bash
--members 1,2,3 \
--request-target "1=aeron:udp?endpoint=127.0.0.1:40111" \
--request-target "2=aeron:udp?endpoint=127.0.0.1:40112" \
--request-target "3=aeron:udp?endpoint=127.0.0.1:40113"
```

### `parse_request_target()`

This function is called once for each `--request-target` argument.

Given:

```text
2=aeron:udp?endpoint=127.0.0.1:40112
```

it splits the text at `=` and returns node ID `2` plus its Aeron channel. It
rejects missing IDs, missing channels, zero, negative IDs, and non-numeric IDs.

### `parse_member_ids()`

This function turns:

```text
1,2,3
```

into:

```cpp
std::vector<std::int32_t>{1, 2, 3}
```

It rejects empty and duplicate member IDs, such as `1,2,2` or `1,2,`.

### `validate_request_targets()`

This function checks the complete configuration before network activity begins.
It verifies that:

- at least one target exists;
- every node ID is positive;
- every Aeron channel is non-empty;
- no node ID is repeated;
- no channel is repeated;
- every required Raft member has a request target; and
- no request target belongs to a node outside the declared membership.

These checks catch configuration errors early. For example, the client should
not start if node 3 is a cluster member but its request channel is missing.

## Constructing the router

The client constructs the router during startup:

```cpp
LeaderRouter router(options.request_targets, options.members);
```

Its initial state is effectively:

```cpp
discovery_index_ = 0;
leader_index_ = std::nullopt;
observed_term_ = -1;
state_ = LeaderKnowledgeState::unknown;
```

This means:

```text
Start discovery with the first configured target.
No leader is currently known.
No Raft term has been observed yet.
```

`leader_index_` is an index into the target vector, not a node ID:

```text
targets_[0] = node 1
targets_[1] = node 2
targets_[2] = node 3
```

## Choosing the first target

The client calls:

```cpp
std::size_t current_target = router.preferred_target();
```

`preferred_target()` returns the known leader when one exists. Otherwise, it
returns the current discovery target:

```cpp
return leader_index_.value_or(discovery_index_);
```

At startup there is no known leader, so the first result is index `0`, which is
node 1. The client then asks `AeronClientSession` to send the command:

```cpp
session.offer(current_target, command);
```

## Learning from responses with `observe()`

`observe()` is the main routing method. The client calls it when a matching
response arrives over Aeron.

### 1. Check the response term

Raft terms are leadership epochs. A higher term represents newer leadership
information.

If the response term is negative, the response is invalid. If the router has
already observed term 8 and receives a delayed term-7 response, the result is
`stale`. The old response is not allowed to replace newer routing knowledge.

When a response has a newer term, the router records the term and immediately
forgets its previous leader:

```cpp
observed_term_ = response.term;
leader_index_.reset();
state_ = LeaderKnowledgeState::unknown;
```

This is necessary because a leader remembered from an older term may no longer
be leader in the new term.

### 2. Check the leader ID

The response may contain:

```text
-1       -> leader is currently unknown
1, 2, 3  -> known cluster member
```

Values such as `0`, `-2`, or an unconfigured node like `99` are invalid.

### 3. Handle an unknown leader

During an election, a follower may know that it is not the leader without yet
knowing who the new leader is. It returns `leader_id=-1`.

The router clears its cached leader, enters `unknown`, and the client continues
round-robin discovery.

### 4. Handle a leader hint

Suppose node 1 responds:

```text
status=not_leader leader_id=2 term=5
```

The router uses `target_index(2)` to translate node ID 2 into target-vector
index 1. It stores that index as the preferred leader target.

Because this information came indirectly from a follower, the state becomes
`suspect`. The client immediately sends the same command to node 2.

If node 2 returns:

```text
status=ok leader_id=2 term=5
```

the router now has direct proof and changes the state to `known`.

## Complete successful request sequence

Assume node 2 is leader, but the client starts by trying node 1:

```text
1. preferred_target() chooses node 1 because no leader is known.

2. AeronClientSession sends the command to node 1.

3. Node 1 replies: not_leader, leader_id=2, term=5.

4. observe() records node 2 as the suspected leader.

5. preferred_target() now chooses node 2.

6. AeronClientSession sends the same command and request ID to node 2.

7. Node 2 replicates, commits, and applies the command.

8. Node 2 replies: ok, leader_id=2, term=5.

9. observe() confirms node 2 and changes the router state to known.
```

The request ID stays unchanged across retries. This is important because the
outcome of an earlier attempt may be uncertain. The replicated deduplication
logic ensures that retrying the same request does not apply it twice.

## Discovery when nobody provides a hint

If a node responds with `leader_id=-1`, the client calls:

```cpp
router.next_discovery_target(current_target);
```

The implementation advances through the target vector and wraps at the end:

```cpp
discovery_index_ = (previous_target + 1) % targets_.size();
```

For three nodes, the sequence is:

```text
node 1 -> node 2 -> node 3 -> node 1 -> ...
```

This is leader discovery, not request load balancing. Normal commands must
eventually reach the leader.

## Handling an unavailable target

If Aeron reports `NOT_CONNECTED` or `PUBLICATION_CLOSED`, or an offered request
does not receive a response before its response timeout, the client calls:

```cpp
router.mark_target_unavailable(current_target);
```

If the unavailable target was the cached leader, the method clears that leader
and returns the router to the `unknown` state. It also prepares the next target
for discovery.

Temporary Aeron conditions such as back pressure are handled differently. They
do not prove that the target is unavailable, so the recovery policy waits and
retries rather than immediately forgetting the leader.

## The small accessor methods

### `leader_id()`

Returns the currently cached leader node ID, or `std::nullopt` when no leader is
known.

### `state()`

Returns `unknown`, `suspect`, or `known`.

### `observed_term()`

Returns the newest term accepted by the router. Older responses cannot move
this value backward.

### `target_count()`

Returns the number of configured request targets.

### `target(index)`

Returns the complete `RequestTarget` at an index. It throws if the supplied
index is outside the vector.

### `target_index(node_id)`

Performs the reverse lookup from Raft node ID to vector index:

```text
node ID 3 -> targets_ index 2
```

It returns `std::nullopt` if the node is not configured.

### `to_string(state)`

Converts the knowledge state into readable logging output:

```text
leader_state=unknown
leader_state=suspect
leader_state=known
```

## Separation of responsibilities

The client deliberately separates three jobs:

```text
LeaderRouter
    chooses which node should receive the next attempt
        |
        v
AeronClientSession
    sends commands and receives responses
        |
        v
ClientRecoveryPolicy
    determines when retries and response timeouts occur
```

Keeping these jobs separate makes the behavior easier to understand and test:

- routing code does not need to know Aeron publication details;
- Aeron code does not decide which node is leader; and
- retry timing does not modify Raft leadership knowledge.

The operational result is that the client can begin without knowing the leader,
learn from follower responses, ignore stale information, survive a leader
change, and safely retry one logical command until it obtains a confirmed
response or reaches its overall deadline.
