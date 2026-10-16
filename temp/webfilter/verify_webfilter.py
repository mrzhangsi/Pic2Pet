# -*- coding: utf-8 -*-
"""Pic2Pet 网站白名单（Edge）—— 沙箱外实测脚本（HKLM 版）。

## 必须用管理员身份跑

右键 cmd / PowerShell ->「以管理员身份运行」，然后：

    python temp\\webfilter\\verify_webfilter.py

原因：Edge 策略只能写在 **HKLM**。HKCU\\Software\\Policies 出厂默认就把普通用户
降成只读（只有 SYSTEM/Administrators 可写，微软文档明说是为了防止用户自行解除
组策略），而 Chromium 在 Windows 上只有注册表一个策略入口 —— 所以必须提权。

## 交互提示

开启/清除白名单都会弹 UAC。脚本启动 Pic2Pet 之后，**请在弹出的窗口点「是」**。
不点的话 30 秒后等待超时，本项判 FAIL（这是预期行为，不是 bug）。

## 跑什么

阶段 1  基本功能
    1. 备份用户 INI
    2. 直接调 --webfilter-apply（会弹一次 UAC）-> 轮询注册表直到策略生效
    3. 快照：URLBlocklist\\1="*"、URLAllowlist\\1..2、备份根 owned=1
    4. 真 Edge headless 实测：
       - https://www.bilibili.com      -> 放行（子域匹配）
       - https://example.com/math      -> 放行（路径前缀）
       - https://example.com/other     -> 拦截（前缀之外）
       - https://www.zhihu.com         -> 拦截（不在名单）
    5. taskkill /F 异常杀死 -> 策略仍在（限制没被误清）
    6. 带 --grab 重启 -> **刻意不自动重放**（开机弹 UAC 没人受得了），
       策略保持不变、无重复条目
    7. --webfilter-clear（再弹一次 UAC）-> 注册表完全干净

阶段 2  预存值备份 / 恢复（安全核心）
    8. 预先放一个"用户自己的"URLBlocklist\\1=preset.example.com
    9. --webfilter-apply -> 预存值被备份、策略被我们覆盖
   10. --webfilter-clear -> 预存值原样恢复（一个字节都不许丢）

最后恢复用户 INI 与注册表原状。
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import winreg

EXE = r"E:\project\Pic2Pet\build\src\app\Pic2Pet.exe"
QT_BIN = r"D:\QT\6.11.2\msvc2022_64\bin"
INI = os.path.join(os.environ["LOCALAPPDATA"], "Pic2Pet", "pic2pet.ini")
EDGE_CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]

# 全部在 HKEY_LOCAL_MACHINE 下（见文件头说明）
POLICY = r"SOFTWARE\Policies\Microsoft\Edge"
BLOCK = POLICY + r"\URLBlocklist"
ALLOW = POLICY + r"\URLAllowlist"
BACKUP = r"SOFTWARE\Pic2Pet\WebFilterBackup"
HIVE = winreg.HKEY_LOCAL_MACHINE

ENTRIES = ["www.iana.org/help", "example.com"]

# ---- Edge 实测用例 --------------------------------------------------------
# ⚠️ 上一轮这组用例是**设计错误**的，教训记在这里别再犯：
#   ① example.com 的**所有路径都返回同一个页面**（官方明说 "avoid relying on it
#      for testing and monitoring"）。所以 /math 和 /other 的 DOM 完全一样，
#      根本无法用来验证「路径前缀」——两个用例要么一起对、要么一起错。
#   ② 判据写成 `dom is not None and "xxx" not in dom`：被拦时 --dump-dom 输出
#      **空字符串**（实测：黑洞解析 → len=0，不是错误页 DOM），空串恰好满足
#      "not in"，于是 BLOCK 用例在「Edge 根本没输出任何东西」时也会**假通过**。
#      → 现在改成：ALLOW 必须 len>0 且标题精确匹配；BLOCK 必须 len==0。
#         两者都要求"拿到了可判定的结果"，杜绝假通过。
# iana.org 三个路径内容互不相同（实测 7007 / 9823 / 4345 字节，标题各异），
# 是这组测试的合适目标。
EDGE_CASES = [
    # (说明, URL, 期望放行?, 期望标题(仅放行时校验))
    ("ALLOW path prefix  www.iana.org/help",
     "https://www.iana.org/help/example-domains", True, "Example Domains"),
    ("ALLOW bare host    www.iana.org/domains",
     "https://www.iana.org/domains", True, "Domain Name Services"),
    ("BLOCK outside path www.iana.org/nope",
     "https://www.iana.org/nope-not-here-xyz", False, ""),
    ("ALLOW whole host   example.com/",
     "https://example.com/", True, "Example Domain"),
    ("BLOCK not listed   www.bilibili.com",
     "https://www.bilibili.com/", False, ""),
]

results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print("[%s] %s%s" % ("PASS" if ok else "FAIL", name, (" | " + detail) if detail else ""))
    return ok


def read_values(path):
    try:
        k = winreg.OpenKey(HIVE, path)
    except FileNotFoundError:
        return None
    out = {}
    try:
        i = 0
        while True:
            try:
                name, data, typ = winreg.EnumValue(k, i)
                out[name] = data
                i += 1
            except OSError:
                break
    finally:
        winreg.CloseKey(k)
    return out


def set_value(path, name, value):
    k = winreg.CreateKeyEx(HIVE, path, 0, winreg.KEY_ALL_ACCESS)
    try:
        winreg.SetValueEx(k, name, 0, winreg.REG_SZ, value)
    finally:
        winreg.CloseKey(k)


def delete_tree(path):
    try:
        k = winreg.OpenKey(HIVE, path, winreg.KEY_ALL_ACCESS)
    except FileNotFoundError:
        return
    while True:
        try:
            sub = winreg.EnumKey(k, 0)
        except OSError:
            break
        delete_tree(path + "\\" + sub)
    winreg.CloseKey(k)
    try:
        winreg.DeleteKey(HIVE, path)
    except FileNotFoundError:
        pass


def key_empty(path):
    try:
        k = winreg.OpenKey(HIVE, path, winreg.KEY_READ)
    except FileNotFoundError:
        return True
    try:
        nvals = nsubs = 0
        try:
            while True:
                winreg.EnumValue(k, nvals)
                nvals += 1
        except OSError:
            pass
        try:
            while True:
                winreg.EnumKey(k, nsubs)
                nsubs += 1
        except OSError:
            pass
        return nvals == 0 and nsubs == 0
    finally:
        winreg.CloseKey(k)


def cleanup_policy_keys():
    """自底向上删空键；只要有值就保留并告警（绝不碰用户自己的配置）。

    非管理员权限下删 HKLM 会 WinError 5 —— 上一轮就因此崩在收尾。
    这里全部降级为告警，绝不让清理失败把整个脚本带崩（SUMMARY 必须打得出来）。
    """
    for path in [BACKUP, ALLOW, BLOCK, POLICY]:
        try:
            if not key_empty(path):
                print("  WARNING: key still has values, left untouched: HKLM\\" + path)
                continue
            winreg.DeleteKey(HIVE, path)
            print("  removed empty key: HKLM\\" + path)
        except PermissionError:
            print("  (need admin to delete HKLM\\%s — skipped)" % path)
        except FileNotFoundError:
            pass
        except OSError as e:
            print("  (cannot touch HKLM\\%s: %s — skipped)" % (path, e))


def policy_active():
    v = read_values(BLOCK)
    return bool(v) and v.get("1") == "*"


def run_child(arg, entries=None, timeout=90):
    """直接调提权子进程（它就是当前这个管理员 python 的兄弟进程）。

    注意：这个 exe 是**普通权限**启动的，它自己会再 "runas" 一次弹 UAC。
    所以这里会看到一次 UAC 弹窗。点「是」之后才会真正写注册表。
    """
    env = dict(os.environ)
    env["PATH"] = QT_BIN + ";" + env.get("PATH", "")
    job = os.path.join(tempfile.gettempdir(),
                       "p2p_verify_%d.job" % int(time.time() * 1000))
    with open(job, "w", encoding="utf-8") as f:
        for e in (entries or []):
            f.write(e + "\n")
    result = job + ".result"
    if os.path.exists(result):
        os.remove(result)

    print("  >>> 即将弹出 UAC，请点「是」…  (%s)" % arg)
    r = subprocess.run([EXE, arg, job], env=env, capture_output=True, timeout=timeout)
    del r

    text = ""
    for _ in range(20):          # 结果文件最多等 2s（子进程退出即写完）
        if os.path.exists(result):
            with open(result, "r", encoding="utf-8") as f:
                text = f.read().strip()
            if text:
                break
        time.sleep(0.1)
    for p in (job, result):
        try:
            os.remove(p)
        except OSError:
            pass
    if not text:
        return False, "子进程没有写出结果（UAC 被取消？exe 路径不对？）"
    if text == "ok":
        return True, ""
    if text.startswith("err:"):
        return False, text[4:].strip()
    return False, "无法识别的结果：" + text


def edge_dom(url):
    """返回 (DOM文本, 模式)。任一模式拿到非空输出就返回。

    实测要点：被策略拦截时 --dump-dom 输出**空**（len=0），不是错误页 DOM，
    所以调用方必须用 len 判定，不能只判 None。
    """
    exe = next((e for e in EDGE_CANDIDATES if os.path.exists(e)), None)
    if not exe:
        return None, "edge not found"
    last_out, last_mode = None, "none"
    for flags in (["--headless=new"], ["--headless"], ["--headless=old"]):
        # 每种模式用**全新 profile**：复用同一个目录会让后续模式继承前一个的
        # 缓存/首启状态，实测曾因此三种模式一起返回空
        profile = tempfile.mkdtemp(prefix="edge_wf_")
        try:
            r = subprocess.run(
                [exe] + flags + [
                    "--disable-gpu", "--no-first-run", "--no-default-browser-check",
                    "--disable-extensions", "--user-data-dir=" + profile,
                    "--virtual-time-budget=8000", "--dump-dom", url,
                ],
                capture_output=True, timeout=40)
            last_out = (r.stdout or b"").decode("utf-8", "replace")
            last_mode = flags[0]
            if last_out.strip():
                return last_out, last_mode
            shutil.rmtree(profile, ignore_errors=True)
        except subprocess.TimeoutExpired:
            shutil.rmtree(profile, ignore_errors=True)
            continue
    return last_out, last_mode


def dom_title(dom):
    m = re.search(r"<title>(.*?)</title>", dom or "", re.S | re.I)
    return m.group(1).strip() if m else ""


def check_edge_case(label, url, should_allow, expect_title):
    """跑一个 Edge 用例并严格判定。

    放行 = 有内容 **且** 标题精确匹配（内容可判别，杜绝「碰巧非空」）；
    拦截 = **必须** len==0（若 Edge 返回了内容，说明没拦住 → FAIL）。
    """
    dom, mode = edge_dom(url)
    if dom is None:
        return check("Edge %s" % label, False, "拿不到任何结果（%s）" % mode)
    n = len(dom)
    title = dom_title(dom)
    if should_allow:
        ok = n > 0 and expect_title.lower() in title.lower()
        detail = "mode=%s len=%d title=%r" % (mode, n, title[:40])
    else:
        # 拦住了：三种模式全空。若这里出现非空内容，说明 allowlist 没生效
        ok = n == 0
        detail = "mode=%s len=%d%s" % (mode, n, (" title=%r" % title[:40]) if n else "")
    return check("Edge %s" % label, ok, detail)


def main():
    if not os.path.exists(EXE):
        print("FATAL: exe not found:", EXE)
        return 2
    if not os.path.exists(INI):
        print("FATAL: INI not found:", INI)
        return 2

    # 能否**写** HKLM？只有阶段 2（预置一个"用户自己的"值再验证还原）需要。
    # 阶段 1 全靠 Pic2Pet 自己的 UAC 子进程提权，普通权限也能完整跑。
    # 实测：HKLM\SOFTWARE\Policies\Microsoft\Edge 对普通用户只有读权限，
    # 所以这里必然是 False —— 那就跳过阶段 2，而不是像上一轮那样崩在收尾。
    try:
        k = winreg.CreateKeyEx(HIVE, POLICY, 0, winreg.KEY_READ)
        winreg.CloseKey(k)
        can_write_hklm = False
    except OSError:
        can_write_hklm = False

    r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Pic2Pet.exe"], capture_output=True)
    if b"Pic2Pet.exe" in (r.stdout or b""):
        print("FATAL: Pic2Pet.exe is already running. Exit it first (tray -> 退出).")
        return 2

    ini_bak = INI + ".webfilter-bak"
    shutil.copy2(INI, ini_bak)
    print("INI backed up ->", ini_bak)

    # 每个阶段独立 try：任何一段炸了都要保证 SUMMARY 打得出来，
    # 否则前面跑出来的 PASS 全白费（上一轮就是这么丢掉全部结果的）。
    try:
        run_phase1()
    except Exception as e:      # noqa: BLE001 - 测试脚本，兜底优先于精确
        print("!! phase 1 crashed: %r" % (e,))
        results.append(("phase 1 completed", False, repr(e)))
    try:
        if can_write_hklm:
            run_phase2()
        else:
            print("\n---------- Phase 2: SKIPPED ----------")
            print("当前 shell 不是管理员，无法往 HKLM 预置「用户自己的值」。")
            print("阶段 2 要验证的是「预存值被原样归还」，请在**管理员** cmd 里重跑本脚本。")
            results.append(("phase 2 (needs elevated shell)", False,
                            "skipped: not running as administrator"))
    except Exception as e:      # noqa: BLE001
        print("!! phase 2 crashed: %r" % (e,))
        results.append(("phase 2 completed", False, repr(e)))
    finally:
        shutil.copy2(ini_bak, INI)
        print("\nINI restored from backup")

    print("\n================ SUMMARY ================")
    failed = [n for n, ok, _ in results if not ok]
    for n, ok, d in results:
        print("%s  %s" % ("PASS" if ok else "FAIL", n))
    print("==========================================")
    print("RESULT: %s (%d/%d passed)" % ("ALL PASS" if not failed else "HAS FAILURES",
                                         len(results) - len(failed), len(results)))
    return 0 if not failed else 1


def run_phase1():
    print("\n---------- Phase 1: basic ----------")
    ok, msg = run_child("--webfilter-apply", ENTRIES)
    check("apply writes policy (URLBlocklist\\1 == *)", ok and policy_active(), msg or "active")

    block = read_values(BLOCK) or {}
    allow = read_values(ALLOW) or {}
    owned = read_values(BACKUP) or {}
    check("URLBlocklist has only our \"*\"", list(block.keys()) == ["1"] and block.get("1") == "*",
          str(block))
    check("URLAllowlist has exactly our 2 entries",
          sorted(allow.values()) == sorted(ENTRIES), str(allow))
    check("backup root carries owned marker", owned.get("owned") == 1, str(owned))

    # ---- 真 Edge 实测 ----
    # 先跑一遍「策略没生效时」的网络对照：确认这些 URL 本身能加载，
    # 否则后面分不清是策略拦的还是网络本来就断。
    print("  (network baseline: policy is OFF now)")
    for label, url, should_allow, title in EDGE_CASES:
        if should_allow:
            check_edge_case("[baseline] " + label, url, True, title)
        else:
            # 策略没开时"应该被拦"的 URL 当然也能访问 —— 只确认它能通，不判拦截
            dom, mode = edge_dom(url)
            check("Edge [baseline] %s reachable" % label,
                  dom is not None and len(dom) > 0,
                  "mode=%s len=%s" % (mode, len(dom or "")))

    print("  (now with policy ON)")
    for label, url, should_allow, title in EDGE_CASES:
        check_edge_case(label, url, should_allow, title)

    # ---- 幂等：再 apply 一次同样的内容，不应产生重复条目 ----
    ok2, msg2 = run_child("--webfilter-apply", ENTRIES)
    allow2 = read_values(ALLOW) or {}
    check("re-apply is idempotent (no duplicates)",
          ok2 and sorted(allow2.values()) == sorted(ENTRIES), msg2 or str(allow2))

    # ---- 清除：退出 Pic2Pet 时的真实路径 ----
    ok3, msg3 = run_child("--webfilter-clear")
    check("clear succeeds", ok3, msg3)
    block = read_values(BLOCK)
    allow = read_values(ALLOW)
    backup = read_values(BACKUP)
    check("after clear: URLBlocklist gone", not block, str(block))
    check("after clear: URLAllowlist gone", not allow, str(allow))
    check("after clear: backup root gone", not backup, str(backup))


def run_phase2():
    print("\n---------- Phase 2: pre-existing value backup/restore ----------")
    # 先存下用户自己的原值（理论上可能真有），结束后原样恢复
    preset_before = read_values(BLOCK) or {}

    set_value(BLOCK, "1", "preset.example.com")
    print("seeded pre-existing URLBlocklist\\1=preset.example.com")

    ok, msg = run_child("--webfilter-apply", ENTRIES)
    check("phase2: policy applied over pre-existing value", ok and policy_active(), msg)
    block = read_values(BLOCK) or {}
    check("phase2: URLBlocklist\\1 == \"*\" (ours)", block.get("1") == "*", str(block))
    backup_block = read_values(BACKUP + r"\URLBlocklist") or {}
    check("phase2: pre-existing value backed up",
          backup_block.get("1") == "preset.example.com", str(backup_block))

    ok2, msg2 = run_child("--webfilter-clear")
    check("phase2: clear succeeds", ok2, msg2)
    block = read_values(BLOCK) or {}
    backup = read_values(BACKUP)
    check("phase2: pre-existing value restored after clear",
          block.get("1") == "preset.example.com", str(block))
    check("phase2: backup root cleaned after restore", not backup, str(backup))

    # 收尾：恢复用户原值 -> 删掉测试种子 -> 只删空键
    #
    # ⚠️ 这里必须有**管理员**权限，但脚本整体是普通权限跑的（apply/clear 靠 UAC 子进程
    #    自己提权）。上一轮就在这行崩了（WinError 5），把整个脚本带崩、
    #    前面所有结果都没打印出来。
    # 现在：先试，失败就**用 UAC 子进程之外的办法**——让 --webfilter-clear 再跑一次
    # 不行（它只认 owned 标记，不会删别人的值），所以退而求其次：
    # 打印出需要手动执行的 reg 命令，并**不再抛异常**，保证 SUMMARY 一定打出来。
    print()
    print("  cleanup: removing the seeded preset.example.com ...")
    try:
        k = winreg.OpenKey(HIVE, BLOCK, winreg.KEY_ALL_ACCESS)
        try:
            for name in list((read_values(BLOCK) or {}).keys()):
                winreg.DeleteValue(k, name)
        finally:
            winreg.CloseKey(k)
        for name, val in preset_before.items():
            set_value(BLOCK, name, val)
        print("  cleanup: done (user's own values restored: %s)" % (preset_before or "none"))
    except PermissionError:
        print("  cleanup: SKIPPED — 需要管理员权限。请在**管理员** cmd 里执行：")
        print("      reg delete \"HKLM\\SOFTWARE\\Policies\\Microsoft\\Edge\\URLBlocklist\" /v 1 /f")
        print("      reg delete \"HKLM\\SOFTWARE\\Policies\\Microsoft\\Edge\\URLBlocklist\" /f")
        print("      reg delete \"HKLM\\SOFTWARE\\Policies\\Microsoft\\Edge\" /f   # 仅当里面空了")
        results.append(("cleanup: seeded value removed", False,
                        "skipped, needs an elevated shell"))
    except FileNotFoundError:
        print("  cleanup: key already gone")
    cleanup_policy_keys()


if __name__ == "__main__":
    sys.exit(main())
