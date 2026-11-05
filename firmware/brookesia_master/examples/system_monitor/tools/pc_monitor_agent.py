#!/usr/bin/env python3
"""PC system monitor agent for the ESP32-P4 "System Monitor" app.

Sends a JSON snapshot over UDP once per second. The board must be connected to
the same LAN (Wi-Fi via Settings > Wi-Fi) and the System Monitor app must be
open (it binds UDP port 9999).

CPU/GPU temperatures come from LibreHardwareMonitor when it is running:

    LibreHardwareMonitor.exe
      Options -> Remote Web Server -> Run   (default port 8085)

The agent polls http://127.0.0.1:8085/data.json and reports the CPU package /
die temperature (and the GPU core temperature when available). When
LibreHardwareMonitor is not running, Windows falls back to the ACPI thermal zone
(`Win32_PerfFormattedData_Counters_ThermalZoneInformation`), which is coarser
than the CPU package sensor but still gives a live CPU temperature.

Usage (Windows / Linux / macOS):
    pip install psutil
    python pc_monitor_agent.py
    python pc_monitor_agent.py --host 192.168.1.50
    python pc_monitor_agent.py --host 192.168.1.255 --interval 0.5
    python pc_monitor_agent.py --lhm-url http://127.0.0.1:8085/data.json
    python pc_monitor_agent.py --check-temps   # print the detected CPU/GPU temps and exit

GPU utilization uses `nvidia-smi` when available; otherwise `gpu_util` is -1
and the board shows "N/A" on the GPU gauge.
"""

from __future__ import annotations

import argparse
import json
import re
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

try:
    import psutil
except ImportError:  # pragma: no cover - friendly hint for end users
    sys.exit("psutil is required: run `pip install psutil` and retry.")


LHM_DEFAULT_URL = "http://127.0.0.1:8085/data.json"

# Sensor names LibreHardwareMonitor uses for the CPU package/die temperature,
# ordered from "most representative" to "nice fallback".
CPU_TEMP_SCORES = {
    "cpu package": 100,
    "cpu (tctl/tdie)": 99,
    "core (tctl/tdie)": 98,
    "cpu cores": 95,
    "core average": 92,
    "core max": 90,
    "ccd1 (tdie)": 88,
    "ccd2 (tdie)": 87,
    "cpu": 85,
    "core #1": 80,
}

CPU_HINTS = ("cpu", "ryzen", "intel", "processor", "athlon", "threadripper")
GPU_HINTS = ("gpu", "nvidia", "radeon", "graphics", "arc", "video")
NOT_CPU_HINTS = ("gpu", "nvidia", "radeon", "graphics", "ssd", "nvme", "storage",
                 "hdd", "drive", "battery", "motherboard", "chipset", "vrm")

_NUMBER_RE = re.compile(r"-?\d+(?:[.,]\d+)?")


def parse_sensor_value(value) -> float | None:
    """Parse a LibreHardwareMonitor value such as "52.0 °C" into 52.0."""
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    match = _NUMBER_RE.search(str(value))
    if not match:
        return None
    try:
        return float(match.group(0).replace(",", "."))
    except ValueError:
        return None


def iter_lhm_nodes(node, ancestors=()):
    """Yield (node, ancestor_texts) for LibreHardwareMonitor's data.json tree."""
    if not isinstance(node, dict):
        return
    yield node, ancestors
    text = str(node.get("Text") or "")
    children = node.get("Children") or []
    if isinstance(children, list):
        for child in children:
            yield from iter_lhm_nodes(child, ancestors + (text,))


class LibreHardwareMonitorReader:
    """Poll LibreHardwareMonitor's web server for CPU/GPU temperatures."""

    def __init__(self, url: str = LHM_DEFAULT_URL, min_interval: float = 1.5):
        self.url = url
        self.min_interval = max(min_interval, 0.5)
        self.cpu_temp: float | None = None
        self.gpu_temp: float | None = None
        self.available = False
        self._last_fetch = 0.0
        self._warned = False
        self.enabled = bool(url)

    def read(self) -> tuple[float | None, float | None]:
        if not self.enabled:
            return None, None
        now = time.monotonic()
        if now - self._last_fetch < self.min_interval:
            return self.cpu_temp, self.gpu_temp
        self._last_fetch = now

        try:
            with urllib.request.urlopen(self.url, timeout=1.5) as response:
                payload = response.read().decode("utf-8", "replace")
            tree = json.loads(payload)
        except Exception:
            # A local web server can answer with a broken status line, drop the
            # connection mid-header or return truncated JSON. None of that may
            # stop the sampler, so every failure degrades to "no reading".
            if not self._warned:
                print(
                    "\nLibreHardwareMonitor unreachable at "
                    f"{self.url}\n"
                    "  Start LibreHardwareMonitor and enable "
                    "Options -> Remote Web Server -> Run (port 8085).\n"
                    "  CPU falls back to the Windows ACPI thermal zone; GPU uses nvidia-smi.\n",
                    flush=True,
                )
                self._warned = True
            self.available = False
            self.cpu_temp = None
            self.gpu_temp = None
            return None, None

        self.available = True
        cpu_best: tuple[int, float] | None = None
        gpu_best: tuple[int, float] | None = None

        for node, ancestors in iter_lhm_nodes(tree):
            node_type = str(node.get("Type") or "")
            children = node.get("Children") or []
            text = str(node.get("Text") or "")
            if node_type != "Temperature" and not (
                not children and "°c" in str(node.get("Value") or "").lower()
            ):
                continue
            value = parse_sensor_value(node.get("Value"))
            if value is None or not (0.0 < value < 150.0):
                continue

            name = text.lower()
            path = " / ".join(ancestors).lower()

            if any(hint in path for hint in GPU_HINTS) or any(hint in name for hint in GPU_HINTS):
                score = 100 if ("core" in name or "gpu" in name) else 40
                if "hot spot" in name or "junction" in name:
                    score = 30
                if gpu_best is None or score > gpu_best[0]:
                    gpu_best = (score, value)
                continue

            if any(hint in path for hint in NOT_CPU_HINTS):
                continue

            score = CPU_TEMP_SCORES.get(name)
            if score is None:
                if not any(hint in path for hint in CPU_HINTS):
                    continue
                if "package" in name:
                    score = 70
                elif "core" in name:
                    score = 60
                else:
                    continue
            if cpu_best is None or score > cpu_best[0]:
                cpu_best = (score, value)

        self.cpu_temp = cpu_best[1] if cpu_best else None
        self.gpu_temp = gpu_best[1] if gpu_best else None
        return self.cpu_temp, self.gpu_temp


class WmiThermalReader:
    """Windows CPU temperature via the ACPI thermal zone (no extra packages).

    LibreHardwareMonitor is still preferred because it reports the real CPU
    package sensor, but this keeps a temperature on screen when it is not running.

    Several Windows builds only expose one of the sources, and the CIM/WMI calls
    can occasionally be slow on a busy machine, so the PowerShell probe tries the
    performance class, the raw performance counter and the ACPI WMI class in one
    process with a generous timeout.
    """

    QUERY = (
        "$ErrorActionPreference='SilentlyContinue';"
        "$v=$null;"
        "try{$v=(Get-CimInstance -ClassName Win32_PerfFormattedData_Counters_"
        "ThermalZoneInformation|Select-Object -First 1).HighPrecisionTemperature}catch{};"
        "if($null -eq $v){try{$v=(Get-Counter '\\Thermal Zone Information(*)\\High Precision "
        "Temperature' -ErrorAction Stop).CounterSamples[0].CookedValue}catch{}};"
        "if($null -eq $v){try{$v=(Get-CimInstance -Namespace root/wmi -ClassName "
        "MSAcpi_ThermalZoneTemperature|Select-Object -First 1).CurrentTemperature}catch{}};"
        "if($null -ne $v){[int][math]::Round($v)}"
    )

    TIMEOUT_S = 12.0

    def __init__(self, min_interval: float = 10.0):
        self.min_interval = min_interval
        self.last_read = 0.0
        self.last_value: float | None = None
        self.available = sys.platform.startswith("win")
        self.warned = False
        self.last_error: str | None = None

    def read(self) -> float | None:
        if not self.available:
            return None
        now = time.monotonic()
        # A failed probe backs off a bit longer so a stuck WMI service does not
        # stall the sampling loop every few seconds.
        interval = self.min_interval if self.last_value is not None else self.min_interval * 3.0
        if (now - self.last_read) < interval:
            return self.last_value
        self.last_read = now
        self.last_value = self._query()
        return self.last_value

    def _warn(self, reason: str) -> None:
        if self.warned:
            return
        self.warned = True
        self.last_error = reason
        print(f"CPU temperature: thermal zone query failed ({reason})")

    def _query(self) -> float | None:
        try:
            process = subprocess.run(
                ["powershell", "-NoProfile", "-NonInteractive", "-Command", self.QUERY],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=self.TIMEOUT_S,
                text=True,
                errors="replace",
            )
        except subprocess.TimeoutExpired:
            self._warn(f"timeout after {self.TIMEOUT_S:.0f}s")
            return None
        except (OSError, subprocess.SubprocessError) as exc:
            self._warn(str(exc))
            return None

        text = (process.stdout or "").strip()
        if not text:
            detail = (process.stderr or "").strip().splitlines()
            self._warn(detail[0] if detail else "no data returned")
            return None
        try:
            raw = float(text.splitlines()[-1].strip())
        except ValueError:
            self._warn(f"unexpected output: {text.splitlines()[-1][:60]!r}")
            return None
        # The ACPI thermal zone is reported in tenths of a Kelvin.
        celsius = raw / 10.0 - 273.15
        if 20.0 < celsius < 120.0:
            return celsius
        return None


def read_psutil_temperature() -> float | None:
    """Linux fallback: coretemp / k10temp / acpitz via psutil."""
    sensors = getattr(psutil, "sensors_temperatures", None)
    if sensors is None:
        return None
    try:
        groups = sensors()
    except (OSError, RuntimeError, NotImplementedError):
        return None
    for group in ("coretemp", "k10temp", "zenpower", "acpitz"):
        for entry in groups.get(group, []) or []:
            label = (getattr(entry, "label", "") or "").lower()
            current = getattr(entry, "current", None)
            if current is None or not (0.0 < float(current) < 150.0):
                continue
            if "package" in label or "tdie" in label or group in ("acpitz", "k10temp"):
                return float(current)
    for entries in groups.values():
        for entry in entries or []:
            current = getattr(entry, "current", None)
            if current is not None and 0.0 < float(current) < 150.0:
                return float(current)
    return None


def read_gpu() -> tuple[float, float, float]:
    """Return (utilization %, memory used MB, memory total MB) or (-1, 0, 0)."""
    try:
        output = subprocess.check_output(
            [
                "nvidia-smi",
                "--query-gpu=utilization.gpu,memory.used,memory.total",
                "--format=csv,noheader,nounits",
            ],
            stderr=subprocess.DEVNULL,
            timeout=2,
            text=True,
        )
    except (OSError, subprocess.SubprocessError):
        return -1.0, 0.0, 0.0

    first_line = output.strip().splitlines()[0] if output.strip() else ""
    parts = [part.strip() for part in first_line.split(",")]
    if len(parts) != 3:
        return -1.0, 0.0, 0.0
    try:
        return float(parts[0]), float(parts[1]), float(parts[2])
    except ValueError:
        return -1.0, 0.0, 0.0


def read_gpu_temp() -> float | None:
    """GPU temperature from the NVIDIA driver. Works without LibreHardwareMonitor."""
    try:
        output = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=temperature.gpu", "--format=csv,noheader,nounits"],
            stderr=subprocess.DEVNULL,
            timeout=2,
            text=True,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    first_line = output.strip().splitlines()[0] if output.strip() else ""
    try:
        value = float(first_line.strip())
    except ValueError:
        return None
    return value if 0.0 < value < 150.0 else None


def read_disks(fallback: str) -> list[dict[str, float | str]]:
    """Return every fixed local volume with used/total bytes and percent."""
    volumes: list[dict[str, float | str]] = []
    seen: set[str] = set()
    try:
        partitions = psutil.disk_partitions(all=False)
    except OSError:
        partitions = []

    for part in partitions:
        mountpoint = part.mountpoint
        if not mountpoint or mountpoint in seen:
            continue
        try:
            usage = psutil.disk_usage(mountpoint)
        except (PermissionError, OSError):
            continue
        if usage.total <= 0:
            continue
        seen.add(mountpoint)
        volumes.append(
            {
                "name": mountpoint,
                "pct": float(usage.percent),
                "used": float(usage.used),
                "total": float(usage.total),
            }
        )

    if not volumes:
        usage = psutil.disk_usage(fallback)
        volumes.append(
            {
                "name": fallback,
                "pct": float(usage.percent),
                "used": float(usage.used),
                "total": float(usage.total),
            }
        )
    return volumes


def format_temp(value: float | None) -> str:
    return "  --" if value is None else f"{value:5.1f}"


def main() -> None:
    parser = argparse.ArgumentParser(description="ESP32-P4 system monitor UDP agent")
    parser.add_argument(
        "--host",
        default="255.255.255.255",
        help="board IP, directed broadcast (e.g. 192.168.1.255) or 255.255.255.255",
    )
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument(
        "--disk",
        default="C:\\" if sys.platform.startswith("win") else "/",
        help="fallback filesystem when no fixed volumes are reported",
    )
    parser.add_argument(
        "--lhm-url",
        default=LHM_DEFAULT_URL,
        help="LibreHardwareMonitor data.json URL (empty string disables it)",
    )
    parser.add_argument(
        "--check-temps",
        action="store_true",
        help="print the detected CPU/GPU temperatures and exit",
    )
    args = parser.parse_args()

    lhm = LibreHardwareMonitorReader(args.lhm_url, min_interval=max(args.interval * 1.5, 1.0))
    wmi_thermal = WmiThermalReader()

    if args.check_temps:
        cpu_temp, gpu_temp = lhm.read()
        if cpu_temp is None:
            cpu_temp = read_psutil_temperature()
        if cpu_temp is None:
            cpu_temp = wmi_thermal.read()
        if gpu_temp is None:
            gpu_temp = read_gpu_temp()
        cpu_text = f"{cpu_temp:.1f} C" if cpu_temp is not None else "unavailable"
        gpu_text = f"{gpu_temp:.1f} C" if gpu_temp is not None else "unavailable"
        print(f"CPU temperature: {cpu_text}")
        print(f"GPU temperature: {gpu_text}")
        if wmi_thermal.last_error:
            print(f"thermal zone error: {wmi_thermal.last_error}")
        return

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    target = (args.host, args.port)

    last_net = psutil.net_io_counters()
    last_time = time.monotonic()
    last_disk_io = psutil.disk_io_counters()
    psutil.cpu_percent(interval=None)

    print(f"sending to {args.host}:{args.port} every {args.interval:.1f}s (Ctrl+C to stop)")
    print(f"temperatures: LibreHardwareMonitor at {args.lhm_url or '(disabled)'}")
    if wmi_thermal.available:
        probe = wmi_thermal.read()
        if probe is not None:
            print(f"temperatures: ACPI thermal zone probe -> {probe:.1f} C")
        else:
            print("temperatures: ACPI thermal zone probe -> unavailable (CPU temperature stays '--')")

    while True:
        time.sleep(args.interval)

        now = time.monotonic()
        elapsed = max(now - last_time, 1e-3)
        net = psutil.net_io_counters()
        net_down = (net.bytes_recv - last_net.bytes_recv) * 8.0 / elapsed / 1_000_000.0
        net_up = (net.bytes_sent - last_net.bytes_sent) * 8.0 / elapsed / 1_000_000.0
        last_net = net
        last_time = now

        try:
            disk_io = psutil.disk_io_counters()
        except Exception:
            disk_io = None
        disk_read = 0.0
        disk_write = 0.0
        if disk_io is not None and last_disk_io is not None:
            disk_read = max(disk_io.read_bytes - last_disk_io.read_bytes, 0) / elapsed / (1024.0 * 1024.0)
            disk_write = max(disk_io.write_bytes - last_disk_io.write_bytes, 0) / elapsed / (1024.0 * 1024.0)
        last_disk_io = disk_io

        cpu_temp, gpu_temp = lhm.read()
        if cpu_temp is None:
            cpu_temp = read_psutil_temperature()
        if cpu_temp is None:
            cpu_temp = wmi_thermal.read()
        if gpu_temp is None:
            gpu_temp = read_gpu_temp()

        memory = psutil.virtual_memory()
        volumes = read_disks(args.disk)
        disk_used = sum(float(v["used"]) for v in volumes)
        disk_total = sum(float(v["total"]) for v in volumes)
        gpu_util, gpu_mem_used, gpu_mem_total = read_gpu()

        payload = {
            "cpu": round(psutil.cpu_percent(interval=None), 1),
            "mem_used": round(memory.used / (1024.0 * 1024.0), 1),
            "mem_total": round(memory.total / (1024.0 * 1024.0), 1),
            "disk_used": round(disk_used / (1024.0 * 1024.0 * 1024.0), 1),
            "disk_total": round(disk_total / (1024.0 * 1024.0 * 1024.0), 1),
            "disk_count": len(volumes[:6]),
            "gpu_util": round(gpu_util, 1),
            "gpu_mem_used": round(gpu_mem_used, 1),
            "gpu_mem_total": round(gpu_mem_total, 1),
            "cpu_temp": round(cpu_temp, 1) if cpu_temp is not None else -1,
            "gpu_temp": round(gpu_temp, 1) if gpu_temp is not None else -1,
            "net_down": round(net_down, 2),
            "net_up": round(net_up, 2),
            "disk_read": round(disk_read, 2),
            "disk_write": round(disk_write, 2),
        }
        for index, volume in enumerate(volumes[:6]):
            payload[f"disk{index}_pct"] = round(float(volume["pct"]), 1)
            payload[f"disk{index}_name"] = str(volume["name"])

        sock.sendto(json.dumps(payload).encode("utf-8"), target)
        worst_disk = max((float(v["pct"]) for v in volumes), default=0.0)
        source = "" if cpu_temp is None else (" [LHM]" if lhm.available else "")
        print(
            f"\rCPU {payload['cpu']:5.1f}%  MEM {memory.percent:5.1f}%  "
            f"DISK {worst_disk:5.1f}% ({len(volumes)})  GPU {payload['gpu_util']:5.1f}%  "
            f"TEMP CPU {format_temp(cpu_temp)}C / GPU {format_temp(gpu_temp)}C{source:<6} "
            f"NET {net_down:6.2f}/{net_up:6.2f} Mb/s  "
            f"IO {disk_read:5.2f}/{disk_write:5.2f} MB/s",
            end="",
            flush=True,
        )


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nstopped")
