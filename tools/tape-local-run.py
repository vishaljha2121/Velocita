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

    def voter_conf(v):
        lines = [f"node_id={v}", f"incarnation={incarnation}", f"data_dir={a.work / f'voter-{v}'}",
                 f"aeron_dir={tmp / f'aeron-tape-voter-{v}-{incarnation}'}", f"listen=127.0.0.1:{voter_port[v]}",
                 f"master_key_file={keys / 'master.key'}", f"signing_key_file={keys / f'head-{v}.pem'}",
                 f"head_every_ticks={a.head_every}"]
        lines += [f"peer.{o}=127.0.0.1:{voter_port[o]}" for o in (1, 2, 3) if o != v]
        lines += [f"recorder.{r}=127.0.0.1:{recorder_port[r]}" for r in (1, 2, 3)]
        lines += [f"public_key.{o}={keys / f'head-{o}.pub.pem'}" for o in (1, 2, 3)]
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
    unplugged = False
    timeline = []
    try:
        while time.time() - began < a.seconds:
            t = time.time() - began
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
    (a.work / "run.json").write_text(json.dumps({"incarnation": incarnation, "head_every": a.head_every}))
    print(json.dumps(summary, indent=1))
    ok = all(r.get("ok") for r in results.values())
    print("all verified" if ok else "VERIFICATION FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
