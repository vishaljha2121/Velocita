# ADR 0004: MVP V1 does not trade perpetuals

Date: 2026-09-15
Status: Accepted

## Context

Verification of the True Markets contract established that the DeFi surface this
account can trade is Hyperliquid PERPETUAL futures, not spot. The documentation
is explicit: `GET /positions` is described as "Query a user's open Hyperliquid
perpetual positions".

Phase 5 was built on an assumption that is true of spot limit orders and false of
perps: that every position change descends from an effect the cluster authorized.
`apply_observe_fill` enforces it directly - a fill with no matching submit effect
is refused `venue_observation_not_applicable`.

A perp venue moves a position with no order at all in at least three ways:
liquidation, auto-deleveraging, and funding. None carries an `effect_id`, so no
command in the kernel can represent any of them. Offered today, each is refused -
correctly by the contract - and the cluster's position silently diverges from the
venue's. The divergence is DETECTABLE, since `GET /positions` is authoritative
and queryable, but there is currently nothing the gateway could SUBMIT to close
it.

Two further consequences: perp maintenance margin is assessed against a POSITION
and moves with the mark price, while `RiskAccount` reserves against an ORDER and
releases when it resolves - different quantities, and the second has no
representation. And leverage means a 20 USDC budget no longer bounds the loss at
20 USDC.

## Decision

MVP V1 will not trade perpetuals. No perpetual position, liquidation, ADL,
funding, leverage or margin-maintenance contract is added to the reducer for V1.

This is a fail-closed scope decision. It is NOT a claim that the existing spot
accounting can safely approximate perpetuals.

## Consequences

The verified DeFi surface is not a V1 execution target, so the create-order
schema probe and the unverified DeFi economics - maker/taker fee, default
leverage, per-fill on-chain cost - are deferred with it.

Phase 5's work is not wasted. Idempotent effect identity, ambiguous-send
reconciliation, the evidence-to-commit seam and the crash cuts all apply to any
venue. What is deferred is the accounting for position changes the cluster did
not author.

## Alternatives rejected

A NEW OBSERVATION KIND for unsolicited position change, adjudicated
cumulative-by-value like `order_reconciled`. This is the smallest change that
preserves "the reducer owns economics", and it remains the recommended design
when perpetuals are picked up. It is a material byte-contract change and was not
justified for a V1 that will not trade them.

TREAT ANY UNEXPLAINED DIVERGENCE AS `conflict` AND ESCALATE. Safe and cheap, and
it turns an ordinary funding payment into an operator incident several times a
day - which means it will be ignored, and then it protects nothing.

APPROXIMATE PERPS WITH SPOT ACCOUNTING. Rejected outright. The reducer would be
reporting a position it has no mechanism to keep true.

## Before perpetuals are picked up

Implement the deferred ledger-first design in `V2_DEFERRED_FUNCTIONAL_SCOPE.md`:
separately identified external executions and funding settlements, with an
authoritative account snapshot used as a reconciliation CHECK rather than a blind
overwrite. Exact bytes stay unfrozen until the venue's exposed identities and
finality fields are verified.
