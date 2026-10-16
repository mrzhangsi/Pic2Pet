"""用离屏抓取验证设置窗口的某一页（绕开全屏截图被遮挡的问题）。

用法：
    python temp/edgewatch/grab_settings.py 6
会临时写配置（开白名单检测 + 3 条目），跑 `--settings=6=<out.png>`，然后还原原配置。
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, "build", "src", "app", "Pic2Pet.exe")
OUT = os.path.join(ROOT, "temp", "edgewatch")
SETTINGS = os.path.join(os.environ["LOCALAPPDATA"], "Pic2Pet", "Pic2Pet.ini")
os.makedirs(OUT, exist_ok=True)

entries = ["github.com", "docs.python.org", "https://www.bilibili.com/video"]


def main():
    page = sys.argv[1] if len(sys.argv) > 1 else "6"
    out = os.path.join(OUT, "page%s.png" % page)

    subprocess.run(["taskkill", "/IM", "Pic2Pet.exe", "/F"], capture_output=True)

    prev = ""
    if os.path.exists(SETTINGS):
        with open(SETTINGS, "r", encoding="utf-8", errors="replace") as f:
            prev = f.read()
    with open(SETTINGS, "w", encoding="utf-8") as f:
        f.write("[teacher\\webFilter]\nenabled=1\n")
        for i, e in enumerate(entries, 1):
            f.write("%d=%s\n" % (i, e))

    env = dict(os.environ)
    env["PATH"] = r"D:\QT\6.11.2\msvc2022_64\bin;" + env.get("PATH", "")
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    r = subprocess.run([EXE, "--no-tray", "--settings=%s=%s" % (page, out)],
                       env=env, capture_output=True, timeout=40)
    txt = (r.stdout or b"").decode("utf-8", errors="replace")
    for line in txt.splitlines():
        if "GRAB" in line or "edgewatch" in line:
            print("  |", line)
    print("[grab]", out, "存在:" , os.path.exists(out),
          os.path.getsize(out) if os.path.exists(out) else 0, "bytes")

    with open(SETTINGS, "w", encoding="utf-8", errors="replace") as f:
        f.write(prev)


if __name__ == "__main__":
    main()
