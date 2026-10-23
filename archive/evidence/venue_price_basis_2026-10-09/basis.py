"""Compare True Markets' public SOL reference price with Binance SOLUSDT one-second closes.

    python3 basis.py true_markets_sol_1d_5s.json [binance_cache.json]

Both inputs are public and need no key. The True Markets file is the reply of
GET /v1/defi/market/prices/history?symbol=SOL&window=1d&resolution=5s. The Binance bars are
fetched by GET from data-api.binance.vision for the same span and cached if a path is given.
"""
import collections
import datetime
import json
import math
import pathlib
import statistics
import sys
import time
import urllib.request


def venue_points(path):
    document = json.loads(pathlib.Path(path).read_text())
    return [(int(datetime.datetime.fromisoformat(p["t"].replace("Z", "+00:00")).timestamp()), float(p["price"]))
            for p in document["points"]]


def binance_closes(start, end, cache):
    if cache and pathlib.Path(cache).exists():
        document = json.loads(pathlib.Path(cache).read_text())
        if document.get('timestamp_convention') != 'close_available_v1':
            raise ValueError('cache timestamp convention is not close_available_v1')
        if document.get('start') != start or document.get('end') != end:
            raise ValueError('cache interval does not match the requested interval')
        return {int(k): v for k, v in document['closes'].items()}
    closes = {}
    at = start * 1000
    while at < end * 1000:
        url = f"https://data-api.binance.vision/api/v3/klines?symbol=SOLUSDT&interval=1s&startTime={at}&limit=1000"
        request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(request, timeout=30) as reply:
            page = json.load(reply)
        if not page:
            break
        for row in page:
            if row[0] % 1000 or row[6] != row[0] + 999:
                raise ValueError('invalid one-second candle boundary')
            closes[(row[6] + 1) // 1000] = float(row[4])
        at = page[-1][0] + 1000
        time.sleep(0.2)
    if cache:
        pathlib.Path(cache).write_text(json.dumps(dict(timestamp_convention='close_available_v1', start=start, end=end, closes=closes)))
    return closes


def quantile(values, q):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(q * len(ordered)))]


def correlation(xs, ys):
    mx, my = statistics.mean(xs), statistics.mean(ys)
    top = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    return top / math.sqrt(sum((x - mx) ** 2 for x in xs) * sum((y - my) ** 2 for y in ys))


def main(argv):
    venue = venue_points(argv[1])
    closes = binance_closes(venue[0][0] - 120, venue[-1][0] + 120, argv[2] if len(argv) > 2 else None)
    span = venue[-1][0] - venue[0][0]
    spacing = collections.Counter(b[0] - a[0] for a, b in zip(venue, venue[1:]))
    regular = spacing.get(5, 0)
    print(f"venue points {len(venue)} over {span / 3600:.2f} h; binance bars {len(closes)}")
    print(f"venue spacing: {regular} intervals of 5 s, {sum(spacing.values()) - regular} longer "
          f"(longest {max(spacing)} s); seconds not covered at 5 s: {span - 5 * regular} of {span}")
    for lag in (-10, -5, -1, 0, 1, 5, 10):
        basis = [(p / closes[t + lag] - 1) * 1e4 for t, p in venue if t + lag in closes]
        mean = statistics.mean(basis)
        far = sum(abs(x - mean) > 10 for x in basis) / len(basis)
        print(f"venue / binance(t{lag:+d}s) - 1: n={len(basis)} mean={mean:+.2f} bp sd={statistics.pstdev(basis):.2f} "
              f"p05={quantile(basis, .05):+.2f} p95={quantile(basis, .95):+.2f} beyond 10 bp of mean={far:.2%}")
    hourly = collections.defaultdict(list)
    for t, p in venue:
        if t in closes:
            hourly[t // 3600].append((p / closes[t] - 1) * 1e4)
    means = [statistics.mean(v) for v in hourly.values() if len(v) >= 50]
    print(f"hourly mean basis: min={min(means):+.2f} bp max={max(means):+.2f} bp over {len(means)} hours")
    moves = {b[0]: math.log(b[1] / a[1]) for a, b in zip(venue, venue[1:]) if b[0] - a[0] == 5}
    for lag in (-10, -5, -3, -2, -1, 0, 1, 2, 3, 5, 10):
        pairs = [(r, math.log(closes[t + lag] / closes[t + lag - 5])) for t, r in moves.items()
                 if t + lag in closes and t + lag - 5 in closes]
        print(f"5 s return correlation with binance shifted {lag:+d} s: "
              f"{correlation([a for a, _ in pairs], [b for _, b in pairs]):+.3f} (n={len(pairs)})")
    low, high = min(p for _, p in venue), max(p for _, p in venue)
    print(f"venue price range over the span: {(high / low - 1) * 1e4:.0f} bp")


if __name__ == "__main__":
    main(sys.argv)
