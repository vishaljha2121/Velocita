"""Host telemetry for a Raspberry Pi, using only stdlib and /proc + vcgencmd.

Why this is not optional: every latency, staleness, and markout number in this
system is a wall-clock difference. If the Pi's clock drifts, or the SoC throttles
under sustained load, the measurements do not become noisier - they become wrong
in a directional way that looks like a market result. The daily report must be
able to say "this interval ran at 49C with a synced clock" or exclude it.

Nothing here is inferred when it can be read. A value that cannot be obtained on
this host returns None and the report shows a gap.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class HealthSample:
    at_ns: int
    cpu_percent: float | None
    mem_percent: float | None
    disk_percent: float | None
    db_bytes: int | None
    load1: float | None
    clock_offset_ms: float | None
    clock_synced: bool
    throttled_hex: str | None
    temp_celsius: float | None

    def as_row(self) -> dict[str, object]:
        return {
            "cpu_percent": self.cpu_percent,
            "mem_percent": self.mem_percent,
            "disk_percent": self.disk_percent,
            "db_bytes": self.db_bytes,
            "load1": self.load1,
            "clock_offset_ms": self.clock_offset_ms,
            "clock_synced": int(self.clock_synced),
            "throttled_hex": self.throttled_hex,
            "temp_celsius": self.temp_celsius,
        }

    @property
    def is_throttled(self) -> bool:
        if self.throttled_hex is None:
            return False
        try:
            return int(self.throttled_hex, 16) != 0
        except ValueError:
            return False


def _read_load1() -> float | None:
    try:
        return os.getloadavg()[0]
    except (OSError, AttributeError):
        return None


def _read_cpu_percent() -> float | None:
    load = _read_load1()
    if load is None:
        return None
    cpus = os.cpu_count() or 1
    return min(100.0, load / cpus * 100.0)


def _read_mem_percent() -> float | None:
    meminfo = Path("/proc/meminfo")
    if not meminfo.exists():
        return None
    values: dict[str, int] = {}
    for line in meminfo.read_text().splitlines():
        key, _, rest = line.partition(":")
        parts = rest.split()
        if parts:
            values[key] = int(parts[0])
    total = values.get("MemTotal")
    available = values.get("MemAvailable")
    if not total or available is None:
        return None
    return (total - available) / total * 100.0


def _read_disk_percent(path: Path) -> float | None:
    try:
        usage = shutil.disk_usage(path if path.exists() else path.parent)
    except OSError:
        return None
    return usage.used / usage.total * 100.0


def _run(command: list[str], timeout_s: float = 3.0) -> str | None:
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout_s, check=False)
    except (OSError, subprocess.SubprocessError):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def read_throttled() -> str | None:
    output = _run(["vcgencmd", "get_throttled"])
    if output and "=" in output:
        return output.split("=", 1)[1].strip()
    return None


def read_temperature() -> float | None:
    output = _run(["vcgencmd", "measure_temp"])
    if output and "=" in output:
        try:
            return float(output.split("=", 1)[1].removesuffix("'C"))
        except ValueError:
            pass
    thermal = Path("/sys/class/thermal/thermal_zone0/temp")
    if thermal.exists():
        try:
            return int(thermal.read_text().strip()) / 1000.0
        except (OSError, ValueError):
            return None
    return None


def read_clock_status() -> tuple[bool, float | None]:
    tracking = _run(["chronyc", "-c", "tracking"])
    if tracking:
        fields = tracking.split(",")
        if len(fields) > 4:
            try:
                offset_s = float(fields[4])
                return True, offset_s * 1000.0
            except ValueError:
                pass

    status = _run(["timedatectl", "show", "--property=NTPSynchronized", "--value"])
    if status is not None:
        return status.strip() == "yes", None

    return False, None


def sample(*, database_path: Path | None = None) -> HealthSample:
    synced, offset_ms = read_clock_status()
    db_bytes: int | None = None
    if database_path is not None and database_path.exists():
        db_bytes = database_path.stat().st_size

    return HealthSample(
        at_ns=time.time_ns(),
        cpu_percent=_read_cpu_percent(),
        mem_percent=_read_mem_percent(),
        disk_percent=_read_disk_percent(database_path or Path.cwd()),
        db_bytes=db_bytes,
        load1=_read_load1(),
        clock_offset_ms=offset_ms,
        clock_synced=synced,
        throttled_hex=read_throttled(),
        temp_celsius=read_temperature(),
    )


def estimate_disk_requirement(
    *, book_updates_per_second: float, trades_per_minute: float, days: int
) -> dict[str, float]:
    book_row_bytes = 420.0
    trade_row_bytes = 190.0
    frame_row_bytes = 300.0

    seconds = days * 86_400.0
    book_rows = book_updates_per_second * seconds
    trade_rows = trades_per_minute / 60.0 * seconds
    frame_rows = book_rows

    total = book_rows * book_row_bytes + trade_rows * trade_row_bytes + frame_rows * frame_row_bytes
    return {
        "days": float(days),
        "book_rows": book_rows,
        "trade_rows": trade_rows,
        "frame_rows": frame_rows,
        "bytes": total,
        "gigabytes": total / 1_000_000_000.0,
    }
