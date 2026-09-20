"""Run Pic2Pet's own decoder (mediadump) over every fixture in temp/qa and summarise.

This exercises the *real* decode path (MediaLoader -> GifDecoder / ApngDecoder ->
CanvasCompositor), so it catches format-level regressions without launching the GUI.

Usage:  <python> tools/qa_run.py [fixture_dir]

Requires a prior build (build/tools/mediadump/pic2pet_mediadump.exe).
Qt DLLs are located via PIC2PET_QT_BIN or D:/QT/6.11.2/msvc2022_64/bin.
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DUMP = os.path.join(ROOT, "build", "tools", "mediadump", "pic2pet_mediadump.exe")
QT_BIN = os.environ.get("PIC2PET_QT_BIN", r"D:\QT\6.11.2\msvc2022_64\bin")

IMAGE_EXT = (".gif", ".apng", ".png", ".jpg", ".jpeg", ".bmp", ".webp")


def run(fixture, outdir):
    env = dict(os.environ)
    env["PATH"] = QT_BIN + os.pathsep + env.get("PATH", "")
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    p = subprocess.run([DUMP, fixture, outdir], capture_output=True, env=env)
    # 工具的日志是本地代码页（中文 Windows 上是 GBK），不能按 UTF-8 解
    raw = (p.stdout or b"") + (p.stderr or b"")
    text = raw.decode("utf-8", errors="replace")
    if "\ufffd" in text:
        text = raw.decode("gbk", errors="replace")

    info = ""
    warn = ""
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("OK "):
            info = line[3:]
        elif line.startswith("[media]") or line.startswith("[pet]") or "WARNING" in line:
            warn = line
    if not info:
        info = "(decoder failed)" if p.returncode != 0 else "(no output)"
    return info, warn


def main():
    fixture_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "temp", "qa")
    if not os.path.isfile(DUMP):
        print("mediadump not built: %s" % DUMP)
        return 1
    if not os.path.isdir(fixture_dir):
        print("fixture dir not found: %s" % fixture_dir)
        return 1

    names = sorted(
        n for n in os.listdir(fixture_dir) if n.lower().endswith(IMAGE_EXT)
    )
    outroot = os.path.join(ROOT, "temp", "qa_out")

    print("%-24s %s" % ("fixture", "decoder says"))
    print("-" * 78)
    flagged = 0
    for n in names:
        path = os.path.join(fixture_dir, n)
        info, warn = run(path, os.path.join(outroot, os.path.splitext(n)[0]))
        print("%-24s %s" % (n, info or "(no output)"))
        if warn:
            print("%-24s   !! %s" % ("", warn))
            flagged += 1
    print("-" * 78)
    print("%d fixture(s), %d flagged" % (len(names), flagged))
    return 0


if __name__ == "__main__":
    sys.exit(main())
