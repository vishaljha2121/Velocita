"""F-WS: a scripted WebSocket server for recorder tests. Logs every frame it sends and receives, with exact bytes.

Usage: fixture_ws.py PORT SCRIPT.json LOG.jsonl
SCRIPT is a list of steps: {"send": "<text>"}, {"sleep": seconds}, {"close": true},
{"await_subscribe": n} (wait until n client frames arrived), or {"repeat": {...}, "times": n}.
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

    def record(direction, text):
        log.write(json.dumps({"t": time.time_ns(), "dir": direction, "raw": text}) + "\n")
        log.flush()

    async def handler(ws):
        async def reader():
            try:
                async for message in ws:
                    received.append(message)
                    record("in", message if isinstance(message, str) else message.decode())
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
