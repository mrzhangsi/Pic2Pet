# -*- coding: utf-8 -*-
"""Pic2Pet WebFilter 注册表快照 / 清理工具（HKLM 版）。

⚠️ snap 可以普通权限跑；**clean 需要管理员**。

用法：
    python regcheck.py snap     # 打印相关键的当前状态（策略 + 备份，含用户级只读键作对照）
    python regcheck.py clean    # 删掉 Pic2Pet 备份根与 Edge 策略键（仅测试后恢复现场用）
"""
import sys

import winreg

POLICY = r"SOFTWARE\Policies\Microsoft\Edge"
BACKUP = r"SOFTWARE\Pic2Pet\WebFilterBackup"
BLOCK = POLICY + r"\URLBlocklist"
ALLOW = POLICY + r"\URLAllowlist"


def read_key(root, path):
    """返回 {值名: (类型, 数据)}；键不存在返回 None。"""
    try:
        k = winreg.OpenKey(root, path)
    except FileNotFoundError:
        return None
    out = {}
    try:
        i = 0
        while True:
            try:
                name, data, typ = winreg.EnumValue(k, i)
            except OSError:
                break
            out[name] = (typ, data)
            i += 1
    finally:
        winreg.CloseKey(k)
    return out


def subkeys(root, path):
    try:
        k = winreg.OpenKey(root, path)
    except FileNotFoundError:
        return None
    out = []
    try:
        i = 0
        while True:
            try:
                out.append(winreg.EnumKey(k, i))
            except OSError:
                break
            i += 1
    finally:
        winreg.CloseKey(k)
    return out


def fmt(v):
    if v is None:
        return "<键不存在>"
    if not v:
        return "<空键>"
    return ", ".join("%r=%r" % (n, d) for n, (t, d) in sorted(v.items()))


def snap():
    print("[HKLM] URLBlocklist :", fmt(read_key(winreg.HKEY_LOCAL_MACHINE, BLOCK)))
    print("[HKLM] URLAllowlist :", fmt(read_key(winreg.HKEY_LOCAL_MACHINE, ALLOW)))
    print("[HKLM] Edge 子键    :", subkeys(winreg.HKEY_LOCAL_MACHINE, POLICY))
    print("[HKLM] 备份根       :", fmt(read_key(winreg.HKEY_LOCAL_MACHINE, BACKUP)))
    print("[HKLM] 备份子键     :", subkeys(winreg.HKEY_LOCAL_MACHINE, BACKUP))
    # 用户级只作对照：Windows 出厂默认就只读，我们不会往那儿写
    print("[HKCU] URLBlocklist :", fmt(read_key(winreg.HKEY_CURRENT_USER, BLOCK)),
          " <- 永远是空的，我们不写这里")
    print()
    print("提示：edge://policy 能看到 Edge 实际读到的策略")


def delete_tree(root, path):
    """递归删键（仅测试现场恢复用）。"""
    try:
        k = winreg.OpenKey(root, path, winreg.KEY_ALL_ACCESS)
    except FileNotFoundError:
        return
    while True:
        try:
            sub = winreg.EnumKey(k, 0)
        except OSError:
            break
        delete_tree(root, path + "\\" + sub)
    winreg.CloseKey(k)
    try:
        winreg.DeleteKey(root, path)
    except FileNotFoundError:
        pass


def clean():
    try:
        winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, POLICY, winreg.KEY_READ).Close()
    except OSError:
        print("需要**管理员**权限才能清理 HKLM。请以管理员身份重跑。")
        return
    delete_tree(winreg.HKEY_LOCAL_MACHINE, BACKUP)
    delete_tree(winreg.HKEY_LOCAL_MACHINE, BLOCK)
    delete_tree(winreg.HKEY_LOCAL_MACHINE, ALLOW)
    # Edge 策略根只剩自己建的（原本不存在）才删
    subs = subkeys(winreg.HKEY_LOCAL_MACHINE, POLICY)
    if subs == []:
        try:
            winreg.DeleteKey(winreg.HKEY_LOCAL_MACHINE, POLICY)
            print("已删空的 Edge 策略根")
        except FileNotFoundError:
            pass
    print("现场已恢复（HKLM 策略 + 备份）")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "snap"
    if cmd == "snap":
        snap()
    elif cmd == "clean":
        clean()
    else:
        print("未知命令", cmd)
        sys.exit(2)
