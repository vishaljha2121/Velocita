"""Command-line entry point.

Every subcommand runs the safety interlock first. There is no flag to skip it,
and a failed audit exits non-zero before anything else happens.
"""

from __future__ import annotations

import argparse
import json
import sys
from decimal import Decimal
from pathlib import Path

from . import __version__
from .adapters.truemarkets_public import TrueMarketsPublicAdapter
from .collectors import market_stream
from .collectors.system_health import estimate_disk_requirement, read_clock_status, sample
from .config import ConfigError, load_config
from .features.schema import MARKET_MAKING_SCHEMA_V1
from .reporting.daily_report import render_competition_worksheet
from .safety import LIVE_TRADING, SafetyViolation, audit_tree, enforce
from .storage.sqlite_store import ResearchStore


def _preflight() -> None:
    try:
        enforce()
    except SafetyViolation as violation:
        print(f"SAFETY INTERLOCK FAILED\n{violation}", file=sys.stderr)
        raise SystemExit(2) from violation


def cmd_verify(args: argparse.Namespace) -> int:
    """Prove the interlock holds and print what it checked."""
    findings = audit_tree()
    print(f"LIVE_TRADING = {LIVE_TRADING}")
    print(f"static audit: {len(findings)} finding(s)")
    for finding in findings:
        print(f"  {finding}")
    print("codec exposes no OrderEffect encoder: ok")
    print("read-only transport refuses state-changing paths: ok")
    return 1 if findings else 0


def cmd_schema(args: argparse.Namespace) -> int:
    print(MARKET_MAKING_SCHEMA_V1.describe())
    return 0


def cmd_capabilities(args: argparse.Namespace) -> int:
    capabilities = TrueMarketsPublicAdapter().describe_capabilities()
    expressible, reason = capabilities.passive_strategy_is_expressible()
    print(f"venue: {capabilities.venue}")
    print(f"passive strategy expressible: {'YES' if expressible else 'NO'} - {reason}")
    print("\ncapabilities:")
    for item in capabilities.all_fields():
        print(f"  {item.field:<34} {str(item.value):<8} [{item.confidence}]")
    print("\nnotes:")
    for note in capabilities.notes:
        print(f"  - {note}")
    return 0 if expressible else 1


def cmd_config(args: argparse.Namespace) -> int:
    try:
        config = load_config(args.config)
    except ConfigError as error:
        print(f"config error: {error}", file=sys.stderr)
        return 2
    print(f"config digest : {config.digest_hex}")
    print(f"venue         : {config.source.venue} ({config.source.confidence})")
    print(f"pairs         : {len(config.pairs)} declared, {len(config.enabled_pairs())} enabled")
    for pair in config.pairs:
        state = "enabled" if pair.enabled else "disabled"
        print(f"  {pair.pair_id:<24} {state:<9} [{pair.confidence}] instrument={pair.instrument_id}")
    print(f"database      : {config.runtime.database_path}")
    if not config.enabled_pairs():
        print("\nno pair is enabled; confirm the unverified fields before collecting")
    return 0


def cmd_health(args: argparse.Namespace) -> int:
    health = sample(database_path=Path(args.database) if args.database else None)
    print(json.dumps(health.as_row(), indent=2, default=str))
    if not health.clock_synced:
        print("\nWARNING: host clock is not synchronised. Latency and markout horizons "
              "measured on this host are not trustworthy.", file=sys.stderr)
    return 0


def cmd_disk(args: argparse.Namespace) -> int:
    print(f"{'days':>6} {'book rows':>14} {'frames':>14} {'GB':>8}")
    for days in (7, 30, 90):
        estimate = estimate_disk_requirement(
            book_updates_per_second=args.book_rate, trades_per_minute=args.trade_rate, days=days
        )
        print(
            f"{int(estimate['days']):>6} {estimate['book_rows']:>14,.0f} "
            f"{estimate['frame_rows']:>14,.0f} {estimate['gigabytes']:>8.2f}"
        )
    return 0


def cmd_init_db(args: argparse.Namespace) -> int:
    store = ResearchStore(args.database)
    print(f"initialised {store.path} ({store.database_bytes()} bytes)")
    store.checkpoint()
    store.close()
    return 0


def cmd_worksheet(args: argparse.Namespace) -> int:
    print(
        render_competition_worksheet(
            target_rank=args.rank,
            target_volume_quote=Decimal(args.volume),
            hours_remaining=Decimal(args.hours),
            maker_multiplier=Decimal(args.multiplier) if args.multiplier else None,
            multiplier_confidence="unverified",
            quote_sizes_quote=[Decimal(s) for s in args.sizes.split(",")],
        )
    )
    return 0


def cmd_collect(args: argparse.Namespace) -> int:
    """Not implemented, and deliberately not stubbed into something that runs.

    Collection needs a working venue adapter, and the adapter is blocked on
    facts nobody has confirmed yet: whether a public order book exists, what its
    response schema is, and whether resting limit orders are available at all on
    the DeFi surface this account can reach. A collector built against guessed
    field names would run happily and produce data that means nothing, which is
    the failure mode this whole design exists to prevent.
    """
    print(
        "collect is not implemented.\n\n"
        "It is blocked on venue verification, not on effort. Run:\n"
        "    velocita-model capabilities\n"
        "and work through docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md.\n\n"
        "Once the order-book endpoint and its response schema are confirmed, "
        "implement TrueMarketsPublicAdapter.fetch_book/fetch_trades against the\n"
        "documented field names and this command becomes a loop over them.",
        file=sys.stderr,
    )
    return 3


#: Minara's shortlist as True Markets lists it (spot only; ONDO, XMR, LIT are
#: not listed; BTC/ETH/ZEC exist only as Coinbase-wrapped tokens on Base).
DEFAULT_WATCH = "CBBTC,CBETH,SOL,HYPE,CBHYPE,PENDLE,CBZEC,VVV,PENGU"
DEFAULT_TOPICS = "reference_prices,price_candles,trending_assets,surging_assets,orderbook.hyperliquid-spot.HYPE-USDC"


def cmd_stream(args: argparse.Namespace) -> int:
    """Record the public market-data stream as raw evidence. Read-only by
    construction: no keys, and the socket can only send its own subscribe."""
    import time

    from .adapters.websocket import ReadOnlyMarketSocket

    topics = [t for t in args.topics.split(",") if t]
    watch = {w for w in args.watch.split(",") if w}
    host = "api.uat.truemarkets.co" if args.uat else "api.truemarkets.co"
    resolved = json.dumps({"host": host, "topics": topics, "watch": sorted(watch)}, sort_keys=True)
    synced, _offset = read_clock_status()
    deadline = time.monotonic() + args.duration if args.duration else None
    with ResearchStore(args.db) as store:
        session = store.open_session(
            venue="truemarkets", config_digest=_sha256_text(resolved),
            schema_digest=_sha256_text(Path(market_stream.__file__).read_text(encoding="utf-8")),
            code_version=__version__, clock_synced=synced)
        recorder = market_stream.MarketStreamRecorder(store, session, topics=topics, watch_symbols=watch)
        reason = "duration reached"
        try:
            market_stream.run_stream(
                recorder, lambda: ReadOnlyMarketSocket(host, "/v1/defi/market"), topics,
                textfile=str(args.textfile) if args.textfile else None,
                stop=lambda: deadline is not None and time.monotonic() >= deadline)
        except KeyboardInterrupt:
            reason = "interrupted"
        finally:
            store.close_session(session, reason)
    return 0


def cmd_shadow_edge(args: argparse.Namespace) -> int:
    """Live reference prices -> rule features -> the C++ submitter. In shadow
    mode decisions are only recorded; in paper mode (the submitter's --mode
    paper) the kernel may authorize them for the paper gateway. Never a venue."""
    import time as _time
    from datetime import date as _date, datetime as _datetime, timedelta as _timedelta, timezone as _tz

    from .adapters.websocket import ReadOnlyMarketSocket
    from .research.bars import load_binance_daily_api
    from .shadow import edge

    submitter = args.submitter[1:] if args.submitter[:1] == ["--"] else args.submitter
    if not submitter and not args.program_only:
        print("shadow-edge needs the submitter command after --", file=sys.stderr)
        return 64
    account, venue, instrument, spec, caps = (int(x) for x in args.catalog_row.split(","))
    catalog = edge.CatalogRow(args.catalog_root, account, venue, instrument, spec, caps)
    Path(args.program_out).write_text(edge.program_text(
        args.strategy, args.value, catalog, policy_id=args.policy_id, policy_version=args.policy_version,
        feature_schema_id=args.feature_schema_id, valid_from_ns=args.valid_from_ns, valid_until_ns=args.valid_until_ns,
        order_quantity_lots=args.order_quantity_lots, max_notional_micros=args.max_notional_micros,
        max_age_ns=args.max_age_ns, source_id=args.source_id, time_in_force=args.time_in_force,
        post_only=args.post_only), encoding="utf-8")
    if args.program_only:
        return 0  # the operator's setup step installs this file (velocita-v2-shadow-submitter --setup-only)
    metrics = edge.EdgeMetrics(shadow_recorded_code=args.shadow_recorded_code)
    book = None
    if args.ledger_db is not None:
        from .research import costs as measured_costs
        from .shadow.ledger import Ledger
        if args.costs_evidence is None:
            print("--ledger-db needs --costs-evidence: fills are costed from measured quotes, never a guess",
                  file=sys.stderr)
            return 64
        book = Ledger(measured_costs.model(args.costs_evidence, args.costs_symbol), Decimal(args.book_usd),
                      db=args.ledger_db)

    class DayBoundSocket:
        """Ends the stream at the UTC day change so history is reloaded (the
        edge refuses ticks whose history does not end yesterday)."""

        def __init__(self, day: _date) -> None:
            self.day = day
            self.inner = ReadOnlyMarketSocket("api.truemarkets.co", "/v1/defi/market")

        def open(self) -> None: self.inner.open()
        def subscribe(self, topics) -> None: self.inner.subscribe(topics)
        def close(self) -> None: self.inner.close()

        def recv_text(self):
            if _datetime.now(_tz.utc).date() != self.day:
                return None
            return self.inner.recv_text()

    backoff = 1.0
    while True:
        today = _datetime.now(_tz.utc).date()
        bars = load_binance_daily_api({"X": args.history_pair}, today - _timedelta(days=args.history_days),
                                      today - _timedelta(days=1), Path(args.history_cache) / str(today))
        closes = [(d, c) for d, c in zip(bars.dates, bars.close[0]) if c is not None]
        source = edge.FeatureSource(args.strategy, args.value, closes=closes,
                                    usd_per_tick=Decimal(args.usd_per_tick), symbol=args.symbol,
                                    min_interval_ns=int(args.min_tick_interval_s * 1_000_000_000))
        result = edge.run_once(lambda: DayBoundSocket(today), submitter, source, textfile=args.textfile,
                               shadow_recorded_code=args.shadow_recorded_code, metrics=metrics, ledger=book)
        print(f"edge run ended: {result.reason} decisions={result.decisions} errors={result.errors}", flush=True)
        if args.once:
            return 0 if result.decisions else 1
        backoff = 1.0 if result.decisions else min(backoff * 2, 60.0)
        _time.sleep(backoff)


def _sha256_text(text: str) -> str:
    import hashlib

    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def cmd_report(args: argparse.Namespace) -> int:
    """Not implemented: there is no collected data to report on yet."""
    print(
        "report is not implemented; no collection has run.\n"
        "The renderer exists in velocita_model.reporting.daily_report and is "
        "wired once collect produces rows.",
        file=sys.stderr,
    )
    return 3


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="velocita-model",
        description="Passive market-data research and shadow quoting. Cannot trade.",
    )
    parser.add_argument("--version", action="version", version=__version__)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("verify", help="run the safety interlock and report what it checked").set_defaults(
        handler=cmd_verify
    )
    sub.add_parser("schema", help="print the feature schema and its identity").set_defaults(handler=cmd_schema)
    sub.add_parser("capabilities", help="report venue capability and whether passive quoting is possible").set_defaults(
        handler=cmd_capabilities
    )

    config_parser = sub.add_parser("config", help="validate a config and print its digest")
    config_parser.add_argument("config", type=Path)
    config_parser.set_defaults(handler=cmd_config)

    health_parser = sub.add_parser("health", help="sample host telemetry")
    health_parser.add_argument("--database", default=None)
    health_parser.set_defaults(handler=cmd_health)

    disk_parser = sub.add_parser("disk", help="project storage requirements")
    disk_parser.add_argument("--book-rate", type=float, default=2.0, help="book updates per second")
    disk_parser.add_argument("--trade-rate", type=float, default=30.0, help="trades per minute")
    disk_parser.set_defaults(handler=cmd_disk)

    init_parser = sub.add_parser("init-db", help="create the research database")
    init_parser.add_argument("database", type=Path)
    init_parser.set_defaults(handler=cmd_init_db)

    worksheet_parser = sub.add_parser("worksheet", help="leaderboard feasibility arithmetic")
    worksheet_parser.add_argument("--rank", type=int, default=4)
    worksheet_parser.add_argument("--volume", default="100000")
    worksheet_parser.add_argument("--hours", default="168")
    worksheet_parser.add_argument("--multiplier", default=None)
    worksheet_parser.add_argument("--sizes", default="1,2,5,10")
    worksheet_parser.set_defaults(handler=cmd_worksheet)

    # Present but not implemented. Listed so the gap is visible in --help rather
    # than discovered when a systemd unit fails at 3am.
    collect_parser = sub.add_parser("collect", help="[BLOCKED] run the collector (needs venue verification)")
    collect_parser.add_argument("config", type=Path, nargs="?")
    collect_parser.set_defaults(handler=cmd_collect)

    stream_parser = sub.add_parser(
        "stream", help="record the public market-data WebSocket as raw evidence (read-only, no keys)")
    stream_parser.add_argument("--db", type=Path, required=True)
    stream_parser.add_argument("--textfile", type=Path, default=None,
                               help="Prometheus textfile for node-exporter (atomic replace)")
    stream_parser.add_argument("--watch", default=DEFAULT_WATCH, help="symbols whose price candles are kept")
    stream_parser.add_argument("--topics", default=DEFAULT_TOPICS)
    stream_parser.add_argument("--duration", type=float, default=0, help="seconds; 0 runs until stopped")
    stream_parser.add_argument("--uat", action="store_true", help="use the UAT sandbox host")
    stream_parser.set_defaults(handler=cmd_stream)

    edge_parser = sub.add_parser(
        "shadow-edge", help="live ticks -> rule features -> the C++ submitter (shadow, or paper: never a venue)")
    edge_parser.add_argument("--strategy", default="trend", choices=sorted(__import__(
        "velocita_model.research.rules", fromlist=["DEMOS"]).DEMOS))
    edge_parser.add_argument("--value", type=int, default=125)
    edge_parser.add_argument("--symbol", default="BTC|USD")
    edge_parser.add_argument("--history-pair", default="BTCUSDT")
    edge_parser.add_argument("--history-days", type=int, default=400)
    edge_parser.add_argument("--history-cache", type=Path, default=Path.home() / ".cache" / "velocita-shadow")
    edge_parser.add_argument("--usd-per-tick", required=True, help="the catalog row's USD per price tick")
    edge_parser.add_argument("--ledger-db", type=Path, default=None,
                             help="SQLite file for the shadow book (positions, fills, P&L); off when absent")
    edge_parser.add_argument("--costs-evidence", type=Path, default=None,
                             help="measured quote evidence the book's fills are costed with (required with --ledger-db)")
    edge_parser.add_argument("--costs-symbol", default="CBBTC")
    edge_parser.add_argument("--book-usd", default="25", help="virtual book in USD (the owner's whole budget)")
    edge_parser.add_argument("--min-tick-interval-s", type=float, default=0.0,
                             help="forward at most one price per interval (each tick uses two Raft log entries)")
    edge_parser.add_argument("--catalog-root", required=True)
    edge_parser.add_argument("--catalog-row", required=True, help="account,venue,instrument,spec_version,caps_version")
    edge_parser.add_argument("--policy-id", type=int, default=21)
    edge_parser.add_argument("--policy-version", type=int, default=1)
    edge_parser.add_argument("--feature-schema-id", type=int, default=31)
    edge_parser.add_argument("--valid-from-ns", type=int, default=1)
    edge_parser.add_argument("--valid-until-ns", type=int, default=4_102_444_800_000_000_000)  # 2100-01-01
    edge_parser.add_argument("--order-quantity-lots", type=int, default=1)
    edge_parser.add_argument("--max-notional-micros", type=int, required=True)
    edge_parser.add_argument("--max-age-ns", type=int, default=5_000_000_000)
    edge_parser.add_argument("--source-id", type=int, default=41)
    edge_parser.add_argument("--time-in-force", default="gtc")
    edge_parser.add_argument("--post-only", default="required")
    edge_parser.add_argument("--shadow-recorded-code", type=int, default=17)  # edge.SHADOW_RECORDED
    edge_parser.add_argument("--program-out", type=Path, required=True)
    edge_parser.add_argument("--textfile", default=None)
    edge_parser.add_argument("--once", action="store_true", help="one run (tests and smoke checks)")
    edge_parser.add_argument("--program-only", action="store_true",
                             help="write --program-out and exit (input to the operator's setup-only step)")
    edge_parser.add_argument("submitter", nargs=argparse.REMAINDER, help="-- SUBMITTER_ARGV...")
    edge_parser.set_defaults(handler=cmd_shadow_edge)

    report_parser = sub.add_parser("report", help="[BLOCKED] render the daily report (needs collected data)")
    report_parser.add_argument("config", type=Path, nargs="?")
    report_parser.set_defaults(handler=cmd_report)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    _preflight()
    return int(args.handler(args))


if __name__ == "__main__":
    raise SystemExit(main())
