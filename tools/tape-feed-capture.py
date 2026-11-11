#!/usr/bin/env python3
"""Records True Markets' public CeFi feed to a JSONL file for the tape's day-one checks. Read-only: sends only SUBSCRIBE_NO_AUTH."""
import argparse
import asyncio
import json
import time
from pathlib import Path

import websockets

URL = "wss://api.truemarkets.co/v1/cefi/market"
ARENA = ["NVDAC", "TSLAC", "AAPLC", "MSFTC", "AMZNC", "GOOGLC", "METAC", "PLTRC", "MSTRC", "MUC", "SNDKC", "SPCXC"]


async def capture(out: Path, seconds: int, quote: str, channels: list[str]) -> None:
    symbols = [f"{s}-{quote}" for s in ARENA]
    request = {"type": "SUBSCRIBE_NO_AUTH", "item_names": symbols,
               "channels": channels + ["HEARTBEAT"], "timestamp": str(int(time.time()))}
    end = time.monotonic() + seconds
    with out.open("w") as f:
        async with websockets.connect(URL, open_timeout=15, max_size=None) as ws:
            await ws.send(json.dumps(request))
            while time.monotonic() < end:
                try:
                    frame = await asyncio.wait_for(ws.recv(), timeout=max(0.1, end - time.monotonic()))
                except asyncio.TimeoutError:
                    break
                raw = frame if isinstance(frame, str) else frame.decode("utf-8", "replace")
                f.write(json.dumps({"utc_ns": time.time_ns(), "mono_ns": time.monotonic_ns(), "raw": raw}) + "\n")
                f.flush()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out", type=Path)
    parser.add_argument("--seconds", type=int, default=900)
    parser.add_argument("--quote", default="USDC")
    parser.add_argument("--channels", default="INSTRUMENT,EBBO,DEPTH,TRADE")
    a = parser.parse_args()
    a.out.parent.mkdir(parents=True, exist_ok=True)
    asyncio.run(capture(a.out, a.seconds, a.quote, a.channels.split(",")))


if __name__ == "__main__":
    main()
