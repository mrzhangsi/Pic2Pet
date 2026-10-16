"""启动 Pic2Pet（开启 Edge 白名单检测），开一个不在白名单内的页面，等弹窗后全屏截图。

用法：
    python temp/edgewatch/run_e2e.py
会先写好配置（teacher/webFilter 白名单 = example.com），再启动 exe，最后截全屏。
"""
import json
import os
import subprocess
import sys
import time

from PIL import ImageGrab

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, "build", "src", "app", "Pic2Pet.exe")
OUT = os.path.join(ROOT, "temp", "edgewatch")
SETTINGS = os.path.join(os.environ["LOCALAPPDATA"], "Pic2Pet", "Pic2Pet.ini")

os.makedirs(OUT, exist_ok=True)

# 写配置：白名单只放 example.com，下面让 Edge 打开 github.com -> 应报警
def write_cfg(entries, enabled=True):
    os.makedirs(os.path.dirname(SETTINGS), exist_ok=True)
    lines = ["[teacher\\webFilter]", "enabled=%d" % (1 if enabled else 0)]
    # 键从 1 开始编号（TeacherConfig.cpp 里 n > 0 才算有效），别写 size 键
    for i, e in enumerate(entries, 1):
        lines.append("%d=%s" % (i, e))
    prev = ""
    if os.path.exists(SETTINGS):
        with open(SETTINGS, "r", encoding="utf-8", errors="replace") as f:
            prev = f.read()
    with open(SETTINGS, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return prev


def kill_exe():
    subprocess.run(["taskkill", "/IM", "Pic2Pet.exe", "/F"],
                   capture_output=True)


def main():
    url = sys.argv[1] if len(sys.argv) > 1 else "https://github.com/"
    entries = sys.argv[2].split(",") if len(sys.argv) > 2 else ["example.com"]

    kill_exe()
    print("[e2e] settings:", SETTINGS)
    prev = write_cfg(entries)
    backup = os.path.join(OUT, "settings_backup.ini")
    with open(backup, "w", encoding="utf-8", errors="replace") as f:
        f.write(prev)
    print("[e2e] 原配置已备份到", backup)
    print("[e2e] entries:", entries, "-> 打开", url)

    env = dict(os.environ)
    env["PATH"] = r"D:\QT\6.11.2\msvc2022_64\bin;" + env.get("PATH", "")
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    p = subprocess.Popen([EXE, "--no-tray"], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print("[e2e] pid", p.pid, "等 3 秒让监视线程起来")
    time.sleep(3)

    subprocess.Popen(["cmd", "/c", "start", "", url])
    print("[e2e] 已打开页面，等 6 秒等轮询 + 弹窗")
    time.sleep(6)

    shot = os.path.join(OUT, "alert_full.png")
    ImageGrab.grab().save(shot)
    print("[e2e] 截图:", shot)

    time.sleep(1)
    kill_exe()
    try:
        out = p.communicate(timeout=8)[0]
    except subprocess.TimeoutExpired:
        p.kill()
        out = p.communicate()[0]
    txt = (out or b"").decode("utf-8", errors="replace")
    for line in txt.splitlines():
        if "edgewatch" in line or "perf" in line:
            print("  |", line)

    # 还原用户原配置，别把测试数据留在他机器上
    with open(SETTINGS, "w", encoding="utf-8", errors="replace") as f:
        f.write(prev)
    print("[e2e] 已还原原配置")


if __name__ == "__main__":
    main()
