"""EdgeWatch::urlAllowed 匹配逻辑验证（不依赖 Edge，直接跑纯函数）。

用法（项目根目录）：
    python temp/edgewatch/test_urlmatch.py
需要先把 EdgeWatch.cpp 的匹配部分编成 DLL，这里用一个自包含的等价实现对照
——目的是锁住"语义"，不是测 Qt 那一层。
"""

CASES = [
    # (url, entries, expected_allowed, 说明)
    ("https://www.bilibili.com/video/BV1", ["bilibili.com"], True,
     "不带点 = 含子域"),
    ("https://bilibili.com/", ["bilibili.com"], True,
     "主机完全相同"),
    ("https://notbilibili.com/", ["bilibili.com"], False,
     "后缀不算子域（evilbilibili.com 不该放行）"),
    ("https://www.bilibili.com/", [".bilibili.com"], False,
     "前导点 = 精确主机，子域不放行"),
    ("https://bilibili.com/", [".bilibili.com"], True,
     "前导点 = 精确主机，本机放行"),

    # scheme / 端口被剥掉
    ("https://example.com/x", ["https://example.com:8443/x"], True,
     "条目带 scheme 与端口也能匹配"),
    ("https://example.com/y", ["http://example.com/x"], False,
     "scheme 不参与判定，只比主机+路径"),

    # 路径前缀（按路径段）
    ("https://example.com/math", ["example.com/math"], True, "路径全等"),
    ("https://example.com/math/calc", ["example.com/math"], True, "路径下钻"),
    ("https://example.com/mathx", ["example.com/math"], False,
     "/mathx 不是 /math 下的东西（关键用例）"),
    ("https://example.com/math/", ["example.com/math"], True, "带尾斜杠"),
    ("https://example.com/", ["example.com/math"], False, "根路径不放行"),

    # 内部页永远放行
    ("edge://settings", ["bilibili.com"], True, "edge 内部页"),
    ("about:blank", ["bilibili.com"], True, "about 页"),
    ("edge://newtab", ["example.com/math"], True, "内部页优先于条目"),
    ("file:///C:/Users/x/a.html", ["bilibili.com"], True, "本地文件不是上网"),
    ("chrome://version", ["bilibili.com"], True, "chrome 内部页"),
    ("devtools://devtools/bundled/inspector.html", ["bilibili.com"], True, "开发者工具"),
    ("view-source:https://x.com/", ["bilibili.com"], True, "view-source"),

    # 空名单 / 畸形输入：绝不误报
    ("https://example.com/", [], True, "空名单 = 全放行"),
    ("", ["bilibili.com"], True, "空 URL"),
    ("not a url", ["bilibili.com"], True, "解析不出主机 -> 放行"),
    ("https://", ["bilibili.com"], True, "畸形 URL"),

    # 多条目：任一命中即放行
    ("https://github.com/x", ["bilibili.com", "github.com"], True, "命中第二条"),
    ("https://gitlab.com/x", ["bilibili.com", "github.com"], False, "都不命中"),

    # 大小写：URL 与条目都按小写化
    ("https://WWW.Bilibili.COM/x", ["BILIBILI.com"], True, "大小写不敏感"),
    ("https://example.com/MATH", ["example.com/math"], True, "路径大小写不敏感"),

    # 条目本身带空白
    ("https://example.com/x", ["   example.com   "], True, "条目 trim"),

    # 非法条目跳过，不影响其它条目
    ("https://example.com/x", ["", "example.com"], True, "空条目跳过"),
]


def parse_entry(raw):
    e = raw.strip().lower()
    if not e:
        return None
    i = e.find("://")
    if i >= 0:
        e = e[i + 3:]
    slash = e.find("/")
    if slash < 0:
        host, path = e, ""
    else:
        host, path = e[:slash], e[slash:]
    if not host:
        return None
    colon = host.rfind(":")
    if colon > 0:
        host = host[:colon]
    return host, path


INTERNAL = ("edge://", "about:", "chrome://", "devtools://", "view-source:",
            "data:", "javascript:", "blob:", "file://")


def url_allowed(url, entries):
    from urllib.parse import urlparse
    u = url.strip()
    if not u:
        return True
    low = u.lower()
    for p in INTERNAL:
        if low.startswith(p):
            return True
    if not entries:
        return True
    try:
        parsed = urlparse(u)
    except ValueError:
        return True
    host = (parsed.hostname or "").lower()
    if not host:
        return True
    path = (parsed.path or "/").lower()
    for raw in entries:
        pe = parse_entry(raw)
        if pe is None:
            continue
        ehost, epath = pe
        if ehost.startswith("."):
            hm = host == ehost[1:]
        else:
            hm = host == ehost or host.endswith("." + ehost)
        if not hm:
            continue
        if not epath:
            return True
        prefix = epath if epath.endswith("/") else epath + "/"
        if path == epath or path.startswith(prefix):
            return True
    return False


def main():
    failed = 0
    for url, entries, expected, desc in CASES:
        got = url_allowed(url, entries)
        ok = got == expected
        if not ok:
            failed += 1
        print(f"{'PASS' if ok else 'FAIL'}  allowed={got!s:<5} want={expected!s:<5} "
              f"{url[:46]:<46} entries={entries}  # {desc}")
    print()
    print(f"RESULT: {'ALL PASS' if failed == 0 else f'HAS FAILURES'} "
          f"({len(CASES) - failed}/{len(CASES)} passed)")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
