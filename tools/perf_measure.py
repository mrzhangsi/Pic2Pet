"""Measure Pic2Pet's steady-state cost for a given media file.

Launches the app with performance logging on, lets it run for N seconds, then
kills it and summarises the [perf] lines (timerFps / renderFps / cpu / mem).

Usage:  <python> tools/perf_measure.py <media> [seconds]

Requires a prior build (build/src/app/Pic2Pet.exe, dev/console build).
Qt DLLs are located via PIC2PET_QT_BIN or D:/QT/6.11.2/msvc2022_64/bin.
"""

import os
import re
import statistics
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "src", "app", "Pic2Pet.exe")
QT_BIN = os.environ.get("PIC2PET_QT_BIN", r"D:\QT\6.11.2\msvc2022_64\bin")


def parse(line):
    out = {}
    for key, value in re.findall(r"(\w+)=([^\s]+)", line):
        out[key] = value
    return out


def num(row, key):
    raw = row.get(key, "0")
    m = re.match(r"-?\d+(?:\.\d+)?", raw)
    return float(m.group()) if m else 0.0


def main():
    if len(sys.argv) < 2:
        print("usage: perf_measure.py <media> [seconds]")
        return 1
    media = sys.argv[1]
    seconds = 8.0
    repeat = 1
    for arg in sys.argv[2:]:
        if arg.startswith("--repeat="):
            repeat = int(arg.split("=", 1)[1])
        else:
            seconds = float(arg)

    if not os.path.isfile(EXE):
        print("exe not built: %s" % EXE)
        return 1

    env = dict(os.environ)
    env["PATH"] = QT_BIN + os.pathsep + env.get("PATH", "")
    env["QT_FORCE_STDERR_LOGGING"] = "1"

    all_cpu = []
    all_rfps = []
    all_wfps = []
    last_mem = "?"
    for run in range(repeat):
        proc = subprocess.Popen([EXE, "--media", media],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        time.sleep(seconds)
        proc.terminate()
        try:
            raw, _ = proc.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            raw, _ = proc.communicate()

        text = raw.decode("utf-8", errors="replace")
        if "\ufffd" in text:
            text = raw.decode("gbk", errors="replace")

        rows = [parse(line) for line in text.splitlines() if "[perf]" in line]
        if not rows:
            print("run %d: no [perf] lines captured" % (run + 1))
            print(text[-1500:])
            continue

        # 丢掉第一秒（启动/解码），取稳定态
        steady = rows[1:] if len(rows) > 2 else rows
        cpu = [num(r, "cpu") for r in steady]
        rfps = [num(r, "renderFps") for r in steady]
        wfps = [num(r, "wakeFps") for r in steady]
        all_cpu += cpu
        all_rfps += rfps
        all_wfps += wfps
        last_mem = rows[-1].get("mem", "?")

        print("  run %d: cpu avg=%.2f%% max=%.2f%%  wakeFps avg=%.1f  renderFps avg=%.1f  mem(last)=%s"
              % (run + 1, statistics.mean(cpu), max(cpu), statistics.mean(wfps),
                 statistics.mean(rfps), last_mem))

    if not all_cpu:
        return 1

    print("-" * 74)
    print("media: %s   %d x %.0fs" % (media, repeat, seconds))
    print("  CPU     avg=%.2f%%   median=%.2f%%   max=%.2f%%"
          % (statistics.mean(all_cpu), statistics.median(all_cpu), max(all_cpu)))
    print("  renderFps avg=%.1f   wakeFps avg=%.1f   mem(last)=%s   (samples=%d)"
          % (statistics.mean(all_rfps), statistics.mean(all_wfps), last_mem, len(all_cpu)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
