"""Generate QA fixtures for Pic2Pet into temp/qa/.

These are the synthetic / edge-case assets; real-world material (a real MP3,
a real photo, a big animated GIF) still has to be found by hand.

Run:  <venv-python> tools/make_qa_assets.py
Requires: Pillow
"""

import math
import os
import struct
import wave

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "temp", "qa")

ok = []
fail = []


def note(name, why=""):
    ok.append(name if not why else "%s  (%s)" % (name, why))


def err(name, e):
    fail.append("%s  -> %s" % (name, e))


# ---------------------------------------------------------------- helpers

def frame(w, h, color, cx=None, cy=None, r=None, extra=None):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # 注意：cx / cy 要**分别**给默认值——写成一行的 and 会把传进来的 cy 一起覆盖掉
    if cx is None:
        cx = w / 2.0
    if cy is None:
        cy = h / 2.0
    if r is None:
        r = min(w, h) * 0.34
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color + (255,))
    if extra:
        extra(d)
    return img


def save_gif(frames, name, duration_ms, **kw):
    path = os.path.join(OUT, name)
    frames[0].save(
        path,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=False,
        **kw
    )
    return path


def save_apng(frames, name, duration_ms):
    path = os.path.join(OUT, name)
    frames[0].save(
        path,
        format="PNG",
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
    )
    return path


def bob(n, w=240, h=240, phase=0.0):
    """一列上下轻浮的帧。注意 n=2 且 phase=0 时 sin(0)==sin(pi)==0，两帧会完全一样、
    被 Pillow 合并成 1 帧——要测多帧慢速素材必须给个 phase。"""
    return [
        frame(w, h, (0x4C, 0x9A, 0xFF),
              cy=h / 2.0 + math.sin(phase + 2 * math.pi * i / n) * h * 0.05)
        for i in range(n)
    ]


def write_wav(name, freq, ms=180, rate=22050, volume=0.35):
    path = os.path.join(OUT, name)
    n = int(rate * ms / 1000.0)
    fade = max(1, int(rate * 0.01))
    data = bytearray()
    for i in range(n):
        env = min(1.0, i / fade) * max(0.0, 1.0 - i / float(n))
        data += struct.pack("<h", int(volume * env * 32767 * math.sin(2 * math.pi * freq * i / rate)))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(data))
    return path


# ---------------------------------------------------------------- generate

def main():
    os.makedirs(OUT, exist_ok=True)
    BLUE = (0x4C, 0x9A, 0xFF)
    RED = (0xFF, 0x5C, 0x5C)

    # ---- 静态图：三种格式 + 带/不带透明 ----
    for name, fmt, color in [
        ("still.png", "PNG", BLUE),
        ("still.jpg", "JPEG", (0xE8, 0xB0, 0x60)),
        ("still.bmp", "BMP", (0x7C, 0xC0, 0x7C)),
    ]:
        try:
            img = frame(240, 240, color)
            path = os.path.join(OUT, name)
            if fmt == "JPEG":
                img.convert("RGB").save(path, format=fmt, quality=92)
            else:
                img.save(path, format=fmt)
            note(name)
        except Exception as e:      # noqa: BLE001
            err(name, e)

    try:
        img = frame(240, 240, (0xC0, 0x8C, 0xFF))
        img.save(os.path.join(OUT, "still.webp"), format="WEBP", quality=90)
        note("still.webp")
    except Exception as e:          # noqa: BLE001
        err("still.webp", e)

    # 用作"单击"动作的静态图（配合停留时长测试）
    try:
        img = frame(240, 240, (0xFF, 0x9F, 0x43))
        img.save(os.path.join(OUT, "click_still.png"), format="PNG")
        note("click_still.png")
    except Exception as e:          # noqa: BLE001
        err("click_still.png", e)

    # ---- 动图：GIF / APNG / 动画 WebP ----
    try:
        save_gif(bob(24), "anim.gif", 100, disposal=2, transparency=0)
        note("anim.gif", "24f/100ms")
    except Exception as e:          # noqa: BLE001
        err("anim.gif", e)

    try:
        save_apng(bob(12), "anim.apng", 100)
        note("anim.apng", "12f/100ms  <-- 之前完全没测过")
    except Exception as e:          # noqa: BLE001
        err("anim.apng", e)

    try:
        frames = bob(10)
        frames[0].save(
            os.path.join(OUT, "anim.webp"),
            format="WEBP",
            save_all=True,
            append_images=frames[1:],
            duration=100,
            loop=0,
            quality=85,
        )
        note("anim.webp", "程序不支持动图 WebP，应优雅退回静态图")
    except Exception as e:          # noqa: BLE001
        err("anim.webp", e)

    # ---- GIF 边界 ----
    try:
        save_gif(bob(1), "edge_1frame.gif", 100, disposal=2, transparency=0)
        note("edge_1frame.gif", "只有 1 帧，应被当作静态图")
    except Exception as e:          # noqa: BLE001
        err("edge_1frame.gif", e)

    try:
        save_gif(bob(2, phase=math.pi / 2), "edge_slow2s.gif", 2000, disposal=2, transparency=0)
        note("edge_slow2s.gif", "2f/2000ms，验慢速时序")
    except Exception as e:          # noqa: BLE001
        err("edge_slow2s.gif", e)

    try:
        save_gif(bob(60), "edge_fast16ms.gif", 16, disposal=2, transparency=0)
        note("edge_fast16ms.gif", "60f/16ms≈60fps，验高帧率 CPU")
    except Exception as e:          # noqa: BLE001
        err("edge_fast16ms.gif", e)

    # 局部帧：固定蓝方块 + 移动红球（帧只覆盖一小块，验 dispose/blend）
    try:
        frames = []
        for i in range(16):
            img = Image.new("RGBA", (240, 240), (0, 0, 0, 0))
            d = ImageDraw.Draw(img)
            d.rectangle([10, 10, 44, 44], fill=(0x4C, 0x9A, 0xFF, 255))
            cx = 40 + i * 11
            d.ellipse([cx, 100, cx + 56, 156], fill=(0xFF, 0x5C, 0x5C, 255))
            frames.append(img)
        save_gif(frames, "edge_partial.gif", 90, disposal=2, transparency=0)
        note("edge_partial.gif", "局部帧 + 固定元素，验 dispose/blend")
    except Exception as e:          # noqa: BLE001
        err("edge_partial.gif", e)

    # disposal=3（restore to previous）
    try:
        frames = []
        for i in range(6):
            img = Image.new("RGBA", (240, 240), (0, 0, 0, 0))
            d = ImageDraw.Draw(img)
            d.rectangle([20, 20, 120, 120], fill=(0x4C, 0x9A, 0xFF, 255))
            d.ellipse([40 + i * 20, 140, 100 + i * 20, 200], fill=(0xFF, 0x5C, 0x5C, 255))
            frames.append(img)
        save_gif(frames, "edge_disposal3.gif", 200, disposal=3, transparency=0)
        note("edge_disposal3.gif", "synthetic 尽力而为")
    except Exception as e:          # noqa: BLE001
        err("edge_disposal3.gif", e)

    # 交织（interlace）GIF。
    # Pillow 的 GIF 写入器**不支持交织**（save(interlace=True) 会被忽略），所以换个做法：
    #   1) 把行按"交织顺序"重排后存成普通 GIF —— 于是文件里的行序正好就是交织序
    #   2) 手工把 Logical Screen Descriptor 的交织位置 1
    #   3) 用 PIL 读回校验（PIL 会按交织解码），应当还原成原图
    # 图片做成**不透明**，避免 Pillow 裁剪帧、破坏行序假设。
    try:
        src = Image.new("RGB", (240, 240), (255, 255, 255))
        d = ImageDraw.Draw(src)
        for y in range(0, 240, 30):                 # 横向色带：行序错了能一眼看出
            d.rectangle([0, y, 239, y + 9], fill=(0x4C, 0x9A, 0xFF))
        d.ellipse([70, 70, 170, 170], fill=(0xFF, 0x5C, 0x5C))

        order = (list(range(0, 240, 8)) + list(range(4, 240, 8))
                 + list(range(2, 240, 4)) + list(range(1, 240, 2)))
        perm = Image.new("RGB", (240, 240))
        for i, y in enumerate(order):
            perm.paste(src.crop((0, y, 240, y + 1)), (0, i))

        path = os.path.join(OUT, "edge_interlaced.gif")
        perm.save(path, format="GIF", optimize=False)

        blob = bytearray(open(path, "rb").read())
        blob[10] |= 0x40                            # 置交织位
        with open(path, "wb") as f:
            f.write(bytes(blob))

        back = Image.open(path).convert("RGB")
        same = list(back.getdata()) == list(src.getdata())
        note("edge_interlaced.gif", "交织位已置；PIL 还原一致=%s" % same)
    except Exception as e:          # noqa: BLE001
        err("edge_interlaced.gif", e)

    # ---- 宽高比：验双轴缩放不变形 / 换动作不变形 ----
    try:
        save_gif(bob(12, 400, 120), "wide_400x120.gif", 100, disposal=2, transparency=0)
        note("wide_400x120.gif")
    except Exception as e:          # noqa: BLE001
        err("wide_400x120.gif", e)

    try:
        save_gif(bob(12, 120, 400), "tall_120x400.gif", 100, disposal=2, transparency=0)
        note("tall_120x400.gif")
    except Exception as e:          # noqa: BLE001
        err("tall_120x400.gif", e)

    # ---- 音频 ----
    try:
        write_wav("beep.wav", 660.0, 200)
        write_wav("beep2.wav", 990.0, 300)
        note("beep.wav / beep2.wav")
    except Exception as e:          # noqa: BLE001
        err("beep.wav", e)

    # 假 MP3：内容是文本，验错误处理不崩
    try:
        path = os.path.join(OUT, "broken.mp3")
        with open(path, "wb") as f:
            f.write(b"this is definitely not an mp3 file\n" * 8)
        note("broken.mp3", "故意坏的，验 MCI 打不开时不崩")
    except Exception as e:          # noqa: BLE001
        err("broken.mp3", e)

    # ---------------------------------------------------------------- report
    print("")
    print("=" * 60)
    print("generated in %s" % OUT)
    for n in ok:
        print("  OK   %s" % n)
    for n in fail:
        print("  FAIL %s" % n)
    print("=" * 60)


if __name__ == "__main__":
    main()
