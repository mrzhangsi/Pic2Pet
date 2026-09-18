"""Generate a demo pet action set, for testing the action system.

Creates  temp/demo_pet/  containing one GIF per action:

    idle.gif         blue,   gentle bobbing        (loop)
    click.gif        orange, quick "pop"           (once)
    doubleclick.gif  purple, spinning              (once)
    drag.gif         green,  sideways wobble       (loop)

Drop the folder onto the pet window (or pass it as the media argument) to test.

Run:  <venv-python> tools/make_demo_pet.py
Requires: Pillow
"""

import math
import os

from PIL import Image, ImageDraw

SIZE = 200
OUT_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "temp", "demo_pet"
)


def make_frame(color, dx=0.0, dy=0.0, scale=1.0, angle=0.0):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    r = (SIZE * 0.34) * scale
    cx = SIZE / 2.0 + dx
    cy = SIZE / 2.0 + dy
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color + (255,))

    # a white dot offset from the centre, so motion / rotation is obvious
    mr = 8.0 * scale
    mcx = cx + math.cos(math.radians(angle)) * r * 0.55
    mcy = cy + math.sin(math.radians(angle)) * r * 0.55
    d.ellipse([mcx - mr, mcy - mr, mcx + mr, mcy + mr], fill=(255, 255, 255, 255))
    return img


def save(frames, name, duration_ms):
    path = os.path.join(OUT_DIR, name)
    frames[0].save(
        path,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        disposal=2,
        transparency=0,
        optimize=False,
    )
    print("wrote", path, "frames=", len(frames))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    BLUE = (0x4C, 0x9A, 0xFF)
    ORANGE = (0xFF, 0x9F, 0x43)
    PURPLE = (0xA9, 0x7C, 0xFF)
    GREEN = (0x3D, 0xD5, 0x98)

    # idle: 上下轻浮
    n = 12
    save([make_frame(BLUE, dy=math.sin(2 * math.pi * i / n) * 10.0) for i in range(n)],
         "idle.gif", 120)

    # click: 快速弹一下
    scales = [1.0, 1.15, 1.3, 1.42, 1.3, 1.15, 1.05, 1.0]
    save([make_frame(ORANGE, scale=s) for s in scales], "click.gif", 70)

    # doubleclick: 转一圈
    n = 10
    save([make_frame(PURPLE, angle=360.0 * i / n) for i in range(n)], "doubleclick.gif", 70)

    # drag: 左右晃
    n = 10
    save([make_frame(GREEN, dx=math.sin(2 * math.pi * i / n) * 14.0) for i in range(n)],
         "drag.gif", 80)

    print("")
    print("test: pass this folder to the app, e.g.")
    print("  build\\src\\app\\Pic2Pet.exe --media \"%s\"" % OUT_DIR)


if __name__ == "__main__":
    main()
