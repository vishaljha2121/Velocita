"""F-WS: a scripted WebSocket server for recorder tests. Logs every frame it sends and receives, with exact bytes.

Usage: fixture_ws.py PORT SCRIPT.json LOG.jsonl
SCRIPT is a list of steps: {"send": "<text>"}, {"sleep": seconds}, {"close": true},
{"await_subscribe": n} (wait until n client frames arrived), {"repeat": {...}, "times": n}, or {"busy_ms": n, "busy_after_reply_ms": m}.
Like the real feed, it answers every SUBSCRIBE/UNSUBSCRIBE with a CONTROL reply listing the connection's
subscriptions, and while busy_ms after accepting a request it rejects any other with the feed's ERROR reply.
"""
import asyncio
import json
import sys
import time

import websockets


async def main(port, script_path, log_path):
    script = json.load(open(script_path))
    log = open(log_path, "a")
    received = []
    control = {"busy": 0.0, "extra": 0.0, "busy_until": 0.0, "subs": {}}

    def reply(status, message=None):
        subs = [{"channel": c, "item_names": sorted(items)} for c, items in sorted(control["subs"].items()) if items]
        body = {"timestamp": str(time.time_ns()), "channel": "WEBSOCKET", "update": "SNAPSHOT", "status": status}
        if message:
            body["message"] = message
        body["subscriptions"] = [] if status == "ERROR" else subs
        return json.dumps(body, separators=(",", ":"))

    def record(direction, text):
        log.write(json.dumps({"t": time.time_ns(), "dir": direction, "raw": text}) + "\n")
        log.flush()

    async def handler(ws):
        async def reader():
            try:
                async for message in ws:
                    received.append(message)
                    text = message if isinstance(message, str) else message.decode()
                    record("in", text)
                    try:
                        req = json.loads(text)
                    except ValueError:
                        continue
                    if req.get("type") not in ("SUBSCRIBE_NO_AUTH", "UNSUBSCRIBE_NO_AUTH"):
                        continue
                    now = time.monotonic()
                    if now < control["busy_until"]:
                        out = reply("ERROR", "Authentication already in progress")
                        await ws.send(out)
                        record("ctl", out)
                        continue
                    for c in req.get("channels", []):
                        items = control["subs"].setdefault(c, set())
                        if req["type"] == "SUBSCRIBE_NO_AUTH":
                            items.update(req.get("item_names", []))
                        else:
                            items.difference_update(req.get("item_names", []))
                    control["busy_until"] = now + control["busy"] + control["extra"]

                    async def answer(delay):
                        await asyncio.sleep(delay)
                        out = reply("UNAUTHENTICATED")
                        await ws.send(out)
                        record("ctl", out)
                    asyncio.create_task(answer(control["busy"]))
            except websockets.ConnectionClosed:
                pass
        task = asyncio.create_task(reader())
        steps = list(script)
        while steps:
            step = steps.pop(0)
            if "repeat" in step:
                steps[0:0] = [step["repeat"]] * step["times"]
            elif "send" in step:
                await ws.send(step["send"])
                record("out", step["send"])
            elif "busy_ms" in step:
                control["busy"] = step["busy_ms"] / 1000.0
                control["extra"] = step.get("busy_after_reply_ms", 0) / 1000.0
            elif "sleep" in step:
                await asyncio.sleep(step["sleep"])
            elif "await_subscribe" in step:
                while len(received) < step["await_subscribe"]:
                    await asyncio.sleep(0.01)
            elif "close" in step:
                await ws.close()
                break
        await asyncio.sleep(3600)
        task.cancel()

    async with websockets.serve(handler, "127.0.0.1", port, max_size=None, ping_interval=None):
        print("ready", flush=True)
        await asyncio.sleep(3600)


if __name__ == "__main__":
    asyncio.run(main(int(sys.argv[1]), sys.argv[2], sys.argv[3]))
