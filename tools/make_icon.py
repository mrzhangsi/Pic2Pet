"""Generate the Pic2Pet application icon (src/app/app.ico).

Flat white paw print on a rounded blue tile, using the app's brand colour family.
Produces a multi-size .ico (16..256) plus a preview PNG under temp/.

Run:  <venv-python> tools/make_icon.py
Requires: Pillow
"""

import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, os.pardir))
ICO_OUT = os.path.join(ROOT, "src", "app", "app.ico")
PREVIEW_OUT = os.path.join(ROOT, "temp", "app_icon_preview.png")

S = 256          # master canvas size
MARGIN = 8       # transparent border around the tile
RADIUS = 52      # tile corner radius
TOP = (0x5C, 0xA8, 0xFF)
BOT = (0x35, 0x7F, 0xEE)
WHITE = (255, 255, 255, 255)


def _lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def build_master():
    # 1) rounded-tile mask
    tile = Image.new("L", (S, S), 0)
    ImageDraw.Draw(tile).rounded_rectangle(
        [MARGIN, MARGIN, S - 1 - MARGIN, S - 1 - MARGIN], radius=RADIUS, fill=255
    )

    # 2) soft vertical gradient, clipped by the tile mask
    grad = Image.new("RGB", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        gd.line([(0, y), (S, y)], fill=_lerp(TOP, BOT, y / float(S - 1)))

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    img.paste(grad, (0, 0), tile)

    # 3) the paw print itself
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([78, 130, 178, 218], radius=44, fill=WHITE)   # main pad

    toes = [(128 - 54, 112), (128 - 18, 91), (128 + 18, 91), (128 + 54, 112)]
    for cx, cy in toes:
        d.ellipse([cx - 18, cy - 21, cx + 18, cy + 21], fill=WHITE)

    return img


def main():
    master = build_master()

    os.makedirs(os.path.dirname(ICO_OUT), exist_ok=True)
    master.save(
        ICO_OUT,
        format="ICO",
        sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)],
    )

    # Preview strip: full size, then the real small sizes side by side so we can
    # judge legibility at 16/24/32px.
    os.makedirs(os.path.dirname(PREVIEW_OUT), exist_ok=True)
    pv = Image.new("RGBA", (S + 240, S), (255, 255, 255, 255))
    pv.paste(master, (0, 0), master)
    x = S + 12
    for sz in (64, 48, 32, 24, 16):
        ic = master.resize((sz, sz), Image.LANCZOS)
        pv.paste(ic, (x, 8), ic)
        x += sz + 10
    pv.save(PREVIEW_OUT)

    print("wrote " + ICO_OUT)
    print("wrote " + PREVIEW_OUT)


if __name__ == "__main__":
    main()
