"""截设置窗口指定页（开发验证用：界面改动一律截图过一遍）。

用法：
    python temp/edgewatch/shot_settings.py 6
会先备份/改写配置（打开白名单检测 + 3 条目），启动 `--settings=N`，置前，全屏截图，最后还原。
"""
import ctypes
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

page = sys.argv[1] if len(sys.argv) > 1 else "6"
entries = ["github.com", "docs.python.org", "https://www.bilibili.com/video"]


def main():
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
    p = subprocess.Popen([EXE, "--no-tray", "--settings=" + page], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print("[shot] pid", p.pid)
    time.sleep(5)

    # 必须把设置窗口置前，否则截到的是压在它上面的其它窗口
    # （开发期本机前台往往是终端）。设不设 DPI 感知都先设上，免得矩形被虚拟化搞错。
    try:
        ctypes.windll.shcore.SetProcessDpiAwarenessContext(
            ctypes.c_void_p(-4))   # PER_MONITOR_AWARE_V2
    except Exception:
        pass
    user32 = ctypes.windll.user32
    found = []

    EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n <= 0:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        if "Pic2Pet" in buf.value or "设置" in buf.value:
            user32.ShowWindow(hwnd, 9)      # SW_RESTORE
            user32.BringWindowToTop(hwnd)
            user32.SetForegroundWindow(hwnd)
            found.append(buf.value)
        return True

    user32.EnumWindows(EnumProc(cb), 0)
    print("[shot] 置前窗口:", found)
    # 放大到接近全屏：白名单卡片在 AI 教官页最下方，窗口默认高度看不到
    HWND = ctypes.c_void_p
    EnumProc2 = ctypes.WINFUNCTYPE(ctypes.c_bool, HWND, ctypes.c_void_p)

    def cb2(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            n = user32.GetWindowTextLengthW(hwnd)
            if n > 0:
                buf = ctypes.create_unicode_buffer(n + 1)
                user32.GetWindowTextW(hwnd, buf, n + 1)
                if buf.value == "Pic2Pet 设置":
                    user32.SetWindowPos(hwnd, 0, 0, 0, 1080, 1000, 0x0001 | 0x0004)
                    user32.BringWindowToTop(hwnd)
                    user32.SetForegroundWindow(hwnd)
        return True

    user32.EnumWindows(EnumProc2(cb2), 0)
    time.sleep(0.6)

    # 内容区滚到底：白名单卡片在 AI 教官页最下方，窗口再大也未必装得下整页。
    # 抓内容区中央，往下滚到底。
    WM_MOUSEWHEEL = 0x020A
    HWND = ctypes.c_void_p

    def scroll_cb(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            n = user32.GetWindowTextLengthW(hwnd)
            if n > 0:
                buf = ctypes.create_unicode_buffer(n + 1)
                user32.GetWindowTextW(hwnd, buf, n + 1)
                if buf.value == "Pic2Pet 设置":
                    rect = ctypes.create_string_buffer(16)
                    user32.GetWindowRect(hwnd, rect)
                    cx = (ctypes.c_int.from_buffer(rect, 0).value
                          + ctypes.c_int.from_buffer(rect, 4).value) // 2
                    cy = (ctypes.c_int.from_buffer(rect, 8).value
                          + ctypes.c_int.from_buffer(rect, 12).value) // 2
                    lparam = (cy << 16) | (cx & 0xFFFF)
                    # 一次滚到底：delta 很大，Qt 会分派成多次滚动
                    for _ in range(12):
                        user32.SendMessageW(hwnd, WM_MOUSEWHEEL, (ctypes.c_int(-120) << 16) & 0xFFFFFFFF,
                                            lparam)
                    user32.BringWindowToTop(hwnd)
        return True

    user32.EnumWindows(
        ctypes.WINFUNCTYPE(ctypes.c_bool, HWND, ctypes.c_void_p)(scroll_cb), 0)
    time.sleep(1.2)

    shot = os.path.join(OUT, "settings_p%s.png" % page)
    ImageGrab.grab().save(shot)
    print("[shot]", shot)

    time.sleep(0.5)
    subprocess.run(["taskkill", "/IM", "Pic2Pet.exe", "/F"], capture_output=True)
    try:
        p.communicate(timeout=6)
    except subprocess.TimeoutExpired:
        p.kill()
    with open(SETTINGS, "w", encoding="utf-8", errors="replace") as f:
        f.write(prev)
    print("[shot] 已还原原配置")


if __name__ == "__main__":
    main()
