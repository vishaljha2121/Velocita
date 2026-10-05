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


DEFAULT_WATCH = "CBBTC,CBETH,SOL,HYPE,CBHYPE,PENDLE,CBZEC,VVV,PENGU"
DEFAULT_TOPICS = "reference_prices,price_candles,trending_assets,surging_assets,orderbook.hyperliquid-spot.HYPE-USDC"


def cmd_stream(args: argparse.Namespace) -> int:
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


def cmd_md_archive(args: argparse.Namespace) -> int:
    import sqlite3

    from .storage import md_archive as mda

    policy = mda.Policy(retention_days=args.retention_days, keep_archives_days=args.keep_archives_days,
                        grace_s=args.grace_s, max_days=args.max_days, batch_rows=args.batch_rows)
    archiver = mda.Archiver(args.db, args.archive_dir, policy=policy)
    blocked = False
    try:
        with mda.RunLock(args.archive_dir):
            if args.compact:
                archiver.compact(quiet_s=args.quiet_s)
                print(f"md-archive: compacted {args.db}")
                return 0
            ok = False
            try:
                for day in archiver.archive():
                    print(f"md-archive: archived {day}")
                if args.prune:
                    for day, outcome in sorted(archiver.prune().items()):
                        print(f"md-archive: prune {day}: {outcome}")
                        blocked |= outcome in mda.BLOCKED_REASONS
                    for day in archiver.remove_old_archives():
                        print(f"md-archive: removed acknowledged archive {day}")
                ok = True
            finally:
                if args.textfile:
                    mda.write_atomic(Path(args.textfile), archiver.metrics(ok=ok).encode("utf-8"))
    except (mda.ArchiveError, sqlite3.Error) as error:
        print(f"md-archive: {error}", file=sys.stderr)
        return 1
    return 3 if blocked else 0


def cmd_md_restore(args: argparse.Namespace) -> int:
    from .storage import md_archive as mda

    try:
        added = mda.restore(args.archives, args.into)
    except mda.ArchiveError as error:
        print(f"md-restore: {error}", file=sys.stderr)
        return 1
    print(f"md-restore: {added} market_message rows added to {args.into}")
    return 0


def cmd_shadow_edge(args: argparse.Namespace) -> int:
    import time as _time
    from datetime import date as _date, datetime as _datetime, timedelta as _timedelta, timezone as _tz

    from .adapters.websocket import ReadOnlyMarketSocket
    from .research.bars import load_binance_daily_api
    from .shadow import edge, volume
    from .shadow.paper_book import PaperBook

    submitter = args.submitter[1:] if args.submitter[:1] == ["--"] else args.submitter
    if not submitter and not args.program_only:
        print("shadow-edge needs the submitter command after --", file=sys.stderr)
        return 64
    if args.price_source == "candles" and not args.price_chain:
        print("shadow-edge: --price-source candles needs --price-chain (the traded chain)", file=sys.stderr)
        return 64
    account, venue, instrument, spec, caps = (int(x) for x in args.catalog_row.split(","))
    catalog = edge.CatalogRow(args.catalog_root, account, venue, instrument, spec, caps)
    params = volume.Params(args.period_s, args.exit_window_s, args.window_min, args.enter_below_bps,
                           args.exit_above_bps)
    default_policy, default_schema = volume.identity(args.strategy)
    try:
        text = edge.program_text(
            args.strategy, args.value, catalog,
            policy_id=args.policy_id if args.policy_id is not None else default_policy,
            policy_version=args.policy_version,
            feature_schema_id=args.feature_schema_id if args.feature_schema_id is not None else default_schema,
            valid_from_ns=args.valid_from_ns, valid_until_ns=args.valid_until_ns,
            order_quantity_lots=args.order_quantity_lots, max_notional_micros=args.max_notional_micros,
            max_age_ns=args.max_age_ns, source_id=args.source_id, time_in_force=args.time_in_force,
            post_only=args.post_only, params=params if args.strategy in volume.STRATEGIES else None)
    except ValueError as error:
        print(f"shadow-edge: {error}", file=sys.stderr)
        return 64
    Path(args.program_out).write_text(text, encoding="utf-8")
    if args.program_only:
        return 0
    metrics = edge.EdgeMetrics(shadow_recorded_code=args.shadow_recorded_code)
    interval = lambda text: tuple(Decimal(v) for v in text.split(",")) if text else None
    paper_book = PaperBook(clip_lots=args.order_quantity_lots, quote_micros_per_tick_lot=args.quote_micros_per_tick_lot,
                           buy_bps=args.paper_buy_bps, sell_bps=args.paper_sell_bps, cash_usd=Decimal(args.book_usd),
                           weekly_loss_cap_usd=Decimal(args.weekly_loss_cap_usd), db=args.ledger_db,
                           expected_week_volume_usd=interval(args.expected_week_volume_usd),
                           expected_week_loss_usd=interval(args.expected_week_loss_usd))
    window = (volume.MinuteWindow(volume.resolved(args.strategy, params).window_min)
              if args.strategy == "dip" else None)
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

    failures = 0
    while True:
        today = _datetime.now(_tz.utc).date()
        try:
            bars = load_binance_daily_api({"X": args.history_pair}, today - _timedelta(days=args.history_days),
                                          today - _timedelta(days=1), Path(args.history_cache) / str(today))
        except (OSError, ValueError) as error:
            failures += 1
            print(f"edge: history unavailable: {type(error).__name__}: {error}", file=sys.stderr, flush=True)
            _time.sleep(edge.backoff_delay(failures))
            continue
        closes = [(d, c) for d, c in zip(bars.dates, bars.close[0]) if c is not None]
        source = edge.FeatureSource(args.strategy, args.value, closes=closes,
                                    usd_per_tick=Decimal(args.usd_per_tick), symbol=args.symbol,
                                    min_interval_ns=int(args.min_tick_interval_s * 1_000_000_000),
                                    params=params if args.strategy in volume.STRATEGIES else None, window=window,
                                    price_source=args.price_source, price_chain=args.price_chain)
        same_day = lambda day=today: _datetime.now(_tz.utc).date() == day
        result = edge.run_once(lambda: DayBoundSocket(today), submitter, source, textfile=args.textfile,
                               shadow_recorded_code=args.shadow_recorded_code, metrics=metrics, ledger=book,
                               paper_book=paper_book, reconnect=None if args.once else same_day,
                               topics=("price_candles",) if args.price_source == "candles" else ("reference_prices",))
        print(f"edge run ended: {result.reason} decisions={result.decisions} errors={result.errors}", flush=True)
        if args.once:
            return 0 if result.decisions else 1
        failures = 0 if result.decisions else failures + 1
        _time.sleep(edge.backoff_delay(max(failures, 1)))


def _sha256_text(text: str) -> str:
    import hashlib

    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def cmd_report(args: argparse.Namespace) -> int:
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

    archive_parser = sub.add_parser(
        "md-archive", help="archive complete UTC days of stream.db; with --prune delete days the Mac acknowledged")
    archive_parser.add_argument("--db", type=Path, required=True)
    archive_parser.add_argument("--archive-dir", type=Path, required=True)
    archive_parser.add_argument("--textfile", type=Path, default=None, help="Prometheus textfile (atomic replace)")
    archive_parser.add_argument("--prune", action="store_true",
                                help="delete verified, acknowledged days older than --retention-days")
    archive_parser.add_argument("--retention-days", type=int, default=3)
    archive_parser.add_argument("--keep-archives-days", type=int, default=7,
                                help="acknowledged, pruned archives older than this are removed from this host")
    archive_parser.add_argument("--grace-s", type=int, default=600, help="archive a day this long after it ends")
    archive_parser.add_argument("--max-days", type=int, default=10, help="days archived per run")
    archive_parser.add_argument("--batch-rows", type=int, default=5000, help="rows per delete transaction")
    archive_parser.add_argument("--compact", action="store_true",
                                help="VACUUM stream.db (STOP velocita-md-stream first); nothing else runs")
    archive_parser.add_argument("--quiet-s", type=int, default=120,
                                help="--compact refuses if a row arrived within this many seconds")
    archive_parser.set_defaults(handler=cmd_md_archive)

    restore_parser = sub.add_parser("md-restore", help="merge archived days into a research SQLite database")
    restore_parser.add_argument("--into", type=Path, required=True)
    restore_parser.add_argument("archives", type=Path, nargs="+", help="stream-YYYY-MM-DD.sqlite.gz (sidecar beside it)")
    restore_parser.set_defaults(handler=cmd_md_restore)

    edge_parser = sub.add_parser(
        "shadow-edge", help="live ticks -> rule features -> the C++ submitter (shadow, or paper: never a venue)")
    from .shadow.volume import STRATEGIES as VOLUME_STRATEGIES
    edge_parser.add_argument("--strategy", default="trend", choices=sorted(__import__(
        "velocita_model.research.rules", fromlist=["DEMOS"]).DEMOS) + sorted(VOLUME_STRATEGIES))
    edge_parser.add_argument("--value", type=int, default=125, help="the trend SMA in days (trend, trend_churn, rotate)")
    edge_parser.add_argument("--period-s", type=int, default=None,
                             help="churn/trend_churn: seconds per phase (default 60); rotate: rotation period "
                                  "(default 1800)")
    edge_parser.add_argument("--exit-window-s", type=int, default=60,
                             help="rotate: the exit lasts this many seconds at the start of each period")
    edge_parser.add_argument("--window-min", type=int, default=30, help="dip: moving average of the last N minute closes")
    edge_parser.add_argument("--enter-below-bps", type=int, default=20, help="dip: buy below the average by this much")
    edge_parser.add_argument("--exit-above-bps", type=int, default=10, help="dip: sell above the average by this much")
    edge_parser.add_argument("--symbol", default="BTC|USD")
    edge_parser.add_argument("--history-pair", default="BTCUSDT")
    edge_parser.add_argument("--price-source", choices=("reference", "candles"), default="reference",
                             help="reference_prices mid (BTC, ETH) or the 30 s candle close (SOL)")
    edge_parser.add_argument("--price-chain", default=None, help="with --price-source candles: the traded chain")
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
    edge_parser.add_argument("--policy-id", type=int, default=None,
                             help="default: the strategy's (trend 21, churn 22, trend_churn 23, rotate 24, dip 25)")
    edge_parser.add_argument("--policy-version", type=int, default=1)
    edge_parser.add_argument("--feature-schema-id", type=int, default=None,
                             help="default: the strategy's (trend 31, churn 32, trend_churn 33, rotate 34, dip 35)")
    edge_parser.add_argument("--valid-from-ns", type=int, default=1)
    edge_parser.add_argument("--valid-until-ns", type=int, default=4_102_444_800_000_000_000)
    edge_parser.add_argument("--order-quantity-lots", type=int, default=1)
    edge_parser.add_argument("--max-notional-micros", type=int, required=True)
    edge_parser.add_argument("--max-age-ns", type=int, default=5_000_000_000)
    edge_parser.add_argument("--source-id", type=int, default=41)
    edge_parser.add_argument("--time-in-force", default="gtc")
    edge_parser.add_argument("--post-only", default="required")
    edge_parser.add_argument("--shadow-recorded-code", type=int, default=17)
    edge_parser.add_argument("--weekly-loss-cap-usd", default="12",
                             help="paper: stop buys (exits only) while this ISO week's realized loss exceeds it")
    edge_parser.add_argument("--paper-buy-bps", type=int, default=115, help="paper venue buy fee (the gateway's)")
    edge_parser.add_argument("--paper-sell-bps", type=int, default=120, help="paper venue sell fee (the gateway's)")
    edge_parser.add_argument("--quote-micros-per-tick-lot", type=int, default=10,
                             help="the catalog row's notional unit ($1 tick x 0.00001 BTC lot = 10 micros)")
    edge_parser.add_argument("--expected-week-volume-usd", default=None,
                             help="LOW,HIGH: the backtest's weekly volume interval, drawn beside the observed")
    edge_parser.add_argument("--expected-week-loss-usd", default=None,
                             help="LOW,HIGH: the backtest's weekly loss interval, drawn beside the observed")
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
