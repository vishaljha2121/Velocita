#!/usr/bin/env python3
"""Runs a whole tape on one machine: three voters, three recorders and exporters over Aeron on localhost.

Generates keys and configs, runs for a while (optionally stopping and restarting machine N to rehearse an
unplugged host), then verifies every voter's log offline and checks that all voters hold the same history.
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

SYMBOLS = ["NVDAC", "TSLAC", "AAPLC", "MSFTC", "AMZNC", "GOOGLC", "METAC", "PLTRC", "MSTRC", "MUC", "SNDKC", "SPCXC"]


def write(path, text):
    path.write_text(text)
    return path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("build", type=Path, help="build directory with the velocita-tape-* binaries")
    p.add_argument("work", type=Path, help="directory for keys, configs and data (wiped)")
    p.add_argument("--url", default="wss://api.truemarkets.co/v1/cefi/market")
    p.add_argument("--seconds", type=int, default=120)
    p.add_argument("--head-every", type=int, default=10)
    p.add_argument("--unplug", type=int, default=0, help="machine to stop and restart (1-3), 0 for none")
    p.add_argument("--unplug-at", type=int, default=40)
    p.add_argument("--unplug-for", type=int, default=20)
    p.add_argument("--refresh-ms", type=int, default=15 * 60 * 1000)
    p.add_argument("--rotate-at", type=int, default=0, help="seconds into the run to end the segment and start the next")
    a = p.parse_args()

    if a.work.exists():
        shutil.rmtree(a.work)
    keys = a.work / "keys"
    keys.mkdir(parents=True)
    tool = lambda name: str(a.build / name)
    subprocess.run([tool("velocita-tape-keys"), "master", str(keys / "master.key")], check=True)
    for v in (1, 2, 3):
        subprocess.run([tool("velocita-tape-keys"), "head", str(keys / f"head-{v}.pem"), str(keys / f"head-{v}.pub.pem")], check=True)
    incarnation = int.from_bytes(os.urandom(6), "little")
    voter_port = {v: 47100 + v for v in (1, 2, 3)}
    recorder_port = {r: 47200 + r for r in (1, 2, 3)}
    tmp = Path(os.environ.get("TMPDIR", "/tmp"))

    segments = []
    previous_head = {}

    def voter_conf(v):
        lines = [f"node_id={v}", f"incarnation={incarnation}", f"data_dir={a.work / f'voter-{v}'}",
                 f"aeron_dir={tmp / f'aeron-tape-voter-{v}-{incarnation}'}", f"listen=127.0.0.1:{voter_port[v]}",
                 f"master_key_file={keys / 'master.key'}", f"signing_key_file={keys / f'head-{v}.pem'}",
                 f"head_every_ticks={a.head_every}"]
        lines += [f"peer.{o}=127.0.0.1:{voter_port[o]}" for o in (1, 2, 3) if o != v]
        lines += [f"recorder.{r}=127.0.0.1:{recorder_port[r]}" for r in (1, 2, 3)]
        lines += [f"public_key.{o}={keys / f'head-{o}.pub.pem'}" for o in (1, 2, 3)]
        if v in previous_head:
            lines.append(f"previous_head_file={previous_head[v]}")
        return write(a.work / f"voter-{v}.conf", "\n".join(lines) + "\n")

    def recorder_conf(r):
        lines = [f"recorder_id={r}", f"incarnation={incarnation}", f"url={a.url}",
                 "symbols=" + ",".join(s + "-USDC" for s in SYMBOLS), f"aeron_dir={tmp / f'aeron-tape-recorder-{r}-{incarnation}'}",
                 f"listen=127.0.0.1:{recorder_port[r]}", f"master_key_file={keys / 'master.key'}", f"refresh_ms={a.refresh_ms}"]
        lines += [f"voter.{v}=127.0.0.1:{voter_port[v]}" for v in (1, 2, 3)]
        return write(a.work / f"recorder-{r}.conf", "\n".join(lines) + "\n")

    procs = {}

    def start(kind, n):
        conf = voter_conf(n) if kind == "voter" else recorder_conf(n)
        log = open(a.work / f"{kind}-{n}.log", "a")
        procs[(kind, n)] = subprocess.Popen([tool(f"velocita-tape-{kind}"), "--config", str(conf)], stdout=log, stderr=log)

    def stop(kind, n):
        proc = procs.pop((kind, n), None)
        if proc:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=10)

    for v in (1, 2, 3):
        start("voter", v)
    time.sleep(3)
    for r in (1, 2, 3):
        start("recorder", r)
    began = time.time()
    rotated = False
    unplugged = False
    timeline = []
    try:
        while time.time() - began < a.seconds:
            t = time.time() - began
            if a.rotate_at and not rotated and t >= a.rotate_at:
                for r in (1, 2, 3):
                    stop("recorder", r)
                time.sleep(a.head_every + 4)
                for v in (1, 2, 3):
                    stop("voter", v)
                seg = a.work / "segments" / str(incarnation)
                seg.mkdir(parents=True)
                for v in (1, 2, 3):
                    shutil.move(str(a.work / f"voter-{v}"), str(seg / f"voter-{v}"))
                    heads = sorted((seg / f"voter-{v}" / "heads").glob("head-*.json"), key=lambda f: int(f.stem[5:]))
                    previous_head[v] = heads[-1]
                segments.append(incarnation)
                incarnation += 1
                for v in (1, 2, 3):
                    start("voter", v)
                time.sleep(3)
                for r in (1, 2, 3):
                    start("recorder", r)
                rotated = True
                timeline.append(("rotated", incarnation, round(time.time() - began, 1)))
            if a.unplug and not unplugged and t >= a.unplug_at:
                stop("recorder", a.unplug)
                stop("voter", a.unplug)
                unplugged = True
                timeline.append(("unplugged", a.unplug, round(t, 1)))
            if a.unplug and unplugged and ("voter", a.unplug) not in procs and t >= a.unplug_at + a.unplug_for:
                start("voter", a.unplug)
                start("recorder", a.unplug)
                timeline.append(("replugged", a.unplug, round(t, 1)))
            time.sleep(0.5)
        time.sleep(a.head_every + 6)
    finally:
        for key in list(procs):
            stop(*key)

    states = {v: json.loads((a.work / f"voter-{v}" / "state.json").read_text()) for v in (1, 2, 3)}
    results = {}
    for v in (1, 2, 3):
        out = subprocess.run([tool("velocita-tape-verify"), "--wal", str(a.work / f"voter-{v}" / "wal.log"),
                              "--heads", str(a.work / f"voter-{v}" / "heads"), "--incarnation", str(incarnation),
                              "--head-every", str(a.head_every),
                              *sum([["--key", str(keys / f"head-{o}.pub.pem")] for o in (1, 2, 3)], [])],
                             capture_output=True, text=True)
        results[v] = json.loads(out.stdout.strip().splitlines()[-1])
    summary = {"incarnation": incarnation, "timeline": timeline,
               "voters": {v: {"verify_ok": results[v].get("ok"), "tree_size": results[v].get("tree_size"),
                              "root": results[v].get("root"), "heads_verified": results[v].get("heads_verified"),
                              "commit_index": states[v]["commit_index"], "applied_index": states[v]["applied_index"],
                              "update_outcomes": states[v]["counters"]["update_outcomes"],
                              "valid_books": sum(1 for b in states[v]["books"] if b["state"] == "valid")} for v in (1, 2, 3)}}
    links = {}
    for s in segments:
        seg = a.work / "segments" / str(s)
        out = subprocess.run([tool("velocita-tape-verify"), "--wal", str(seg / "voter-1" / "wal.log"),
                              "--heads", str(seg / "voter-1" / "heads"), "--incarnation", str(s), "--head-every", str(a.head_every),
                              *sum([["--key", str(keys / f"head-{o}.pub.pem")] for o in (1, 2, 3)], [])],
                             capture_output=True, text=True)
        links[s] = {"verify_ok": json.loads(out.stdout.strip().splitlines()[-1]).get("ok") if out.stdout.strip() else False}
    if segments:
        prev_tick = results[1].get("previous_segment", {}).get("tick")
        prev_file = a.work / "segments" / str(segments[-1]) / "voter-1" / "heads" / f"head-{prev_tick}.json"
        out = subprocess.run([tool("velocita-tape-verify"), "--wal", str(a.work / "voter-1" / "wal.log"),
                              "--incarnation", str(incarnation), "--previous-head", str(prev_file),
                              *sum([["--key", str(keys / f"head-{o}.pub.pem")] for o in (1, 2, 3)], [])],
                             capture_output=True, text=True)
        last = out.stdout.strip().splitlines()[-1] if out.stdout.strip() else out.stderr.strip()[-300:]
        links["link"] = last
        summary["segments"] = links
        print(json.dumps(links, indent=1))
    (a.work / "run.json").write_text(json.dumps({"incarnation": incarnation, "head_every": a.head_every}))
    print(json.dumps(summary, indent=1))
    ok = all(r.get("ok") for r in results.values())
    if segments:
        ok = ok and all(v.get("verify_ok") for k, v in links.items() if k != "link") and '"linked":true' in links["link"]
    print("all verified" if ok else "VERIFICATION FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
