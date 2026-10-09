"""Replay the running one-second stochastic rule on public one-second bars, using the live indicator and signal classes.

    python3 replay.py DAYS [END_UNIX_SECONDS] [CACHE.json]

Bars are Binance SOLUSDT one-second klines fetched by keyless GET from data-api.binance.vision.
Nothing is sent anywhere else. The rule is not re-implemented here: Indicators, Signals and candle
are imported from model/src/velocita_model/shadow/stochastic_second.py, the module the edge runs.
"""
import collections
import json
import pathlib
import sys
import time
import urllib.request
from decimal import Decimal

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "model" / "src"))
from velocita_model.shadow.stochastic_second import Indicators, Signals, candle

LOTS = 12
MICROS_PER_CENT_LOT = 100


def fee(notional, bps):
    return -(-notional * bps // 10000)


def replay(bars):
    indicators, signals, previous = Indicators(), Signals(), None
    held, last_exit, entry, trips, resets = 0, None, None, [], 0
    for start, end, o, h, l, c in bars:
        bar = candle(start, end, o, h, l, c)
        if indicators.last is not None and bar[0] != indicators.last + 1:
            indicators, signals, previous = Indicators(), Signals(), None
            resets += 1
        flags = previous
        previous = signals.push(indicators.push(bar))
        if flags is None or indicators.count < 1200:
            continue
        exit_flag, entry_flag = flags
        if last_exit is not None and bar[0] - last_exit < 60:
            entry_flag = 0
        price = int(Decimal(c) * 100)
        if held and exit_flag:
            trips.append((entry[0], bar[0], entry[1], price))
            held, last_exit = 0, bar[0]
        elif not held and entry_flag:
            held, entry = LOTS, (bar[0], price)
    return trips, resets, held


def report(bars):
    trips, resets, held = replay(bars)
    days = (bars[-1][0] - bars[0][0]) / 86400000
    print(f"bars {len(bars)} over {days:.2f} days; indicator resets on gaps {resets}; open at end {bool(held)}")
    if not trips:
        print("no round trips")
        return
    gross = [(sell - buy) * LOTS * MICROS_PER_CENT_LOT for _, _, buy, sell in trips]
    volume = sum((buy + sell) * LOTS * MICROS_PER_CENT_LOT for _, _, buy, sell in trips)
    holding = sorted(out - into for into, out, _, _ in trips)
    wins = sum(g > 0 for g in gross)
    print(f"round trips {len(trips)} ({len(trips) / days:.1f} a day); volume ${volume / 1e6:,.2f} (${volume / 1e6 / days:,.2f} a day)")
    print(f"gross P&L ${sum(gross) / 1e6:+.4f}; per round trip {sum(gross) / len(gross) / 1e6 * 100:+.3f} cents; "
          f"winners {wins / len(trips):.1%}; gross per $ of volume {sum(gross) / volume * 1e4:+.2f} bp")
    print(f"holding time: median {holding[len(holding) // 2]} s, p90 {holding[int(.9 * len(holding))]} s, "
          f"in the market {sum(holding) / (days * 86400):.1%} of the time")
    for name, buy_bps, sell_bps in (("paper, 20 bp a side", 20, 20), ("measured, 100 bp buy and 98 bp sell", 100, 98)):
        fees = sum(fee(buy * LOTS * MICROS_PER_CENT_LOT, buy_bps) + fee(sell * LOTS * MICROS_PER_CENT_LOT, sell_bps)
                   for _, _, buy, sell in trips)
        net = sum(gross) - fees
        equity, peak, drawdown, hit = 0, 0, 0, None
        for (into, out, buy, sell), g in zip(trips, gross):
            equity += g - fee(buy * LOTS * MICROS_PER_CENT_LOT, buy_bps) - fee(sell * LOTS * MICROS_PER_CENT_LOT, sell_bps)
            peak = max(peak, equity)
            drawdown = max(drawdown, peak - equity)
            if hit is None and peak - equity >= 10_000_000:
                hit = (out - bars[0][0] // 1000) / 86400
        per_loss = volume / -net if net < 0 else float("inf")
        print(f"{name}: fees ${fees / 1e6:.4f}, net ${net / 1e6:+.4f} (${net / 1e6 / days:+.4f} a day), "
              f"volume per $1 lost ${per_loss:,.0f}, worst drawdown ${drawdown / 1e6:.2f}, "
              f"$10 drawdown reached after {'%.1f days' % hit if hit is not None else 'never'}")
    daily = collections.defaultdict(lambda: [0, 0])
    for (into, out, buy, sell), g in zip(trips, gross):
        day = out // 86400
        daily[day][0] += 1
        daily[day][1] += g
    print("per day (round trips, gross $): " + ", ".join(f"{n} {g / 1e6:+.3f}" for n, g in (daily[d] for d in sorted(daily))))


def fetch(days, end_seconds):
    end = end_seconds * 1000
    at = end - days * 86400 * 1000
    rows = []
    while at < end:
        url = f"https://data-api.binance.vision/api/v3/klines?symbol=SOLUSDT&interval=1s&startTime={at}&limit=1000"
        request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        page = None
        for attempt in range(8):
            try:
                with urllib.request.urlopen(request, timeout=30) as reply:
                    page = json.load(reply)
                break
            except OSError:
                time.sleep(2 + 2 * attempt)
        if page is None:
            raise RuntimeError("the bar download failed eight times in a row")
        if not page:
            break
        rows += [[row[0], row[6], row[1], row[2], row[3], row[4]] for row in page if row[0] < end]
        at = page[-1][0] + 1000
        time.sleep(0.15)
    return rows


if __name__ == "__main__":
    days = int(sys.argv[1])
    end_seconds = int(sys.argv[2]) if len(sys.argv) > 2 else int(time.time()) // 60 * 60
    cache = pathlib.Path(sys.argv[3]) if len(sys.argv) > 3 else None
    if cache and cache.exists():
        bars = json.loads(cache.read_text())
    else:
        bars = fetch(days, end_seconds)
        if cache:
            cache.write_text(json.dumps(bars))
    print(f"window ends {end_seconds} (unix seconds), {days} days")
    report(bars)
