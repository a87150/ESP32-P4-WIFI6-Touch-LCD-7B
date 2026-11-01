#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PC agent for ESP32-P4-WIFI6-Touch-LCD-7B "System Monitor" app.

Broadcasts one JSON datagram per second to UDP port 9999 on the LAN.
The System Monitor app on the 7-inch display listens on the same port.

Usage:
    python pc_agent.py

Optional deps (better data):
    pip install psutil
GPU data requires an NVIDIA card (nvidia-smi is used automatically if present).
"""

import json
import os
import re
import socket
import subprocess
import sys
import time
import platform

PORT = 9999
INTERVAL = 1.0

try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False

GPU_CMD = ["nvidia-smi",
           "--query-gpu=name,utilization.gpu,memory.used,memory.total",
           "--format=csv,noheader,nounits"]


class CpuApi:
    """CPU utilisation: psutil, fallback to Windows GetSystemTimes."""
    def __init__(self):
        self._last_kernel = None
        self._last_user = None
        self._last_total = None

    def read(self):
        if HAS_PSUTIL:
            return psutil.cpu_percent(interval=None)
        return self._windows_read()

    def _windows_read(self):
        import ctypes
        import ctypes.wintypes as wt

        class FT(wt.FILETIME):
            pass

        idle = FT()
        kernel = FT()
        user = FT()
        ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle),
                                             ctypes.byref(kernel),
                                             ctypes.byref(user))
        idle_v = (idle.dwHighDateTime << 32) | idle.dwLowDateTime
        kernel_v = kernel.dwHighDateTime << 32 | kernel.dwLowDateTime
        user_v = user.dwHighDateTime << 32 | user.dwLowDateTime
        total_v = kernel_v + user_v

        if self._last_total is None:
            self._last_idle = idle_v
            self._last_total = total_v
            return 0.0
        didle = idle_v - self._last_idle
        dtotal = total_v - self._last_total
        self._last_idle = idle_v
        self._last_total = total_v
        if dtotal == 0:
            return 0.0
        return (1.0 - didle / dtotal) * 100.0


class MemApi:
    """Memory: psutil, fallback to GlobalMemoryStatusEx (values in KB)."""
    def read(self):
        if HAS_PSUTIL:
            m = psutil.virtual_memory()
            return m.used / (1024 ** 2), m.total / (1024 ** 2)
        import ctypes

        class MEMORYSTATUSEX(ctypes.Structure):
            _fields_ = [("dwLength", ctypes.c_ulong),
                        ("dwMemoryLoad", ctypes.c_ulong),
                        ("ullTotalPhys", ctypes.c_ulonglong),
                        ("ullAvailPhys", ctypes.c_ulonglong),
                        ("ullTotalPageFile", ctypes.c_ulonglong),
                        ("ullAvailPageFile", ctypes.c_ulonglong),
                        ("ullTotalVirtual", ctypes.c_ulonglong),
                        ("ullAvailVirtual", ctypes.c_ulonglong),
                        ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]

        stat = MEMORYSTATUSEX()
        stat.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(stat))
        used_kb = (stat.ullTotalPhys - stat.ullAvailPhys) // 1024
        total_kb = stat.ullTotalPhys // 1024
        return float(used_kb), float(total_kb)


class DiskApi:
    def read(self):
        if HAS_PSUTIL:
            partitions = psutil.disk_partitions()
            used = total = 0.0
            for p in partitions:
                if p.fstype in ("", "tmpfs", "msdosfs"):
                    continue
                try:
                    u = psutil.disk_usage(p.mountpoint)
                    total += u.total
                    used += u.used
                except (PermissionError, OSError):
                    pass
            return used / (1024 ** 2), total / (1024 ** 2)
        # fallback: drive C: only
        if sys.platform == "win32":
            import ctypes
            free = ctypes.c_ulonglong()
            total_bytes = ctypes.c_ulonglong()
            available = ctypes.c_ulonglong()
            ctypes.windll.kernel32.GetDiskFreeSpaceExW("C:",
                                                       ctypes.byref(available),
                                                       ctypes.byref(total_bytes),
                                                       ctypes.byref(free))
            total_kb = total_bytes.value // 1024
            used_kb = (total_bytes.value - available.value) // 1024
            return float(used_kb), float(total_kb)
        return 0.0, 0.0


class GpuApi:
    def __init__(self):
        self._name = ""
        self._ok = False
        self._check_needed = True

    def read(self):
        if self._check_needed:
            try:
                out = subprocess.check_output(GPU_CMD, timeout=5,
                                              universal_newlines=True)
                line = out.strip().splitlines()[0]
                parts = [p.strip() for p in line.split(",")]
                self._name = parts[0]
                self._util = float(parts[1] or 0)
                self._mem_used = float(parts[2] or 0)
                self._mem_total = float(parts[3] or 0)
                self._ok = True
            except Exception:
                self._ok = False
                self._name = ""
            self._check_needed = False
            return (self._util if self._ok else None,
                    self._mem_used if self._ok else 0.0,
                    self._mem_total if self._ok else 0.0,
                    self._name)

        if not self._ok:
            return None, 0.0, 0.0, self._name
        try:
            out = subprocess.check_output(GPU_CMD, timeout=5,
                                          universal_newlines=True)
            line = out.strip().splitlines()[0]
            parts = [p.strip() for p in line.split(",")]
            self._util = float(parts[1] or 0)
            self._mem_used = float(parts[2] or 0)
            self._mem_total = float(parts[3] or 0)
        except Exception:
            pass
        return (self._util, self._mem_used, self._mem_total, self._name)


class NetApi:
    def __init__(self):
        self._last = None
        self._last_time = None

    def read(self):
        """Returns down/up in Mbps (megabits per second)."""
        if not HAS_PSUTIL:
            return 0.0, 0.0
        counters = psutil.net_io_counters()
        now = time.monotonic()
        if self._last is None:
            self._last = counters
            self._last_time = now
            return 0.0, 0.0
        dt = now - self._last_time
        if dt <= 0:
            return 0.0, 0.0
        down_mbps = (counters.bytes_sent - self._last.bytes_sent + 0.0) * 8 / dt / 1e6
        up_mbps = (counters.bytes_recv - self._last.bytes_recv) * 8 / dt / 1e6
        self._last = counters
        self._last_time = now
        return down_mbps, up_mbps


def uptime_s():
    if HAS_PSUTIL:
        return int(time.time() - psutil.boot_time())
    if sys.platform == "win32":
        import ctypes
        return int(ctypes.windll.kernel32.GetTickCount64() // 1000)
    return 0


def main():
    cpu_api = CpuApi()
    mem_api = MemApi()
    disk_api = DiskApi()
    gpu_api = GpuApi()
    net_api = NetApi()

    pc_name = platform.node()[:31]
    payload = {
        "agent": "pc_sysmon",
        "pc": pc_name,
    }

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    print("PC agent running -> broadcasting to UDP :%d every %.1fs" % (PORT, INTERVAL))
    print("psutil:", HAS_PSUTIL, "(pip install psutil for better data)")

    try:
        while True:
            payload["cpu"] = round(cpu_api.read(), 1)
            used, total = mem_api.read()
            payload["mem_used"] = round(used, 1)
            payload["mem_total"] = round(total, 1)
            used, total = disk_api.read()
            payload["disk_used"] = round(used, 1)
            payload["disk_total"] = round(total, 1)
            util, mem_used, mem_total, gpu_name = gpu_api.read()
            payload["gpu_util"] = util if util is not None else 0.0
            payload["gpu_mem_used"] = round(mem_used, 1)
            payload["gpu_mem_total"] = round(mem_total, 1)
            if gpu_name:
                payload["gpu_name"] = gpu_name[:47]
            down, up = net_api.read()
            payload["net_down"] = round(down, 2)
            payload["net_up"] = round(up, 2)
            payload["uptime_s"] = uptime_s()

            data = json.dumps(payload).encode("utf-8")
            sock.sendto(data, ("255.255.255.255", PORT))
            sys.stdout.write("\r%s" % data.decode("utf-8")[:160])
            sys.stdout.flush()
            time.sleep(INTERVAL)
    except KeyboardInterrupt:
        print("\nStopped")


if __name__ == "__main__":
    main()
