"""把 mediadump 导出的帧与 PIL 的合成结果逐像素比对。

用法：
    python tools\compare_frames.py <原始动图> <帧导出目录> [帧数]

    python tools\compare_frames.py test.gif temp\out 24

判定标准：
    alpha              必须完全一致（disposal 语义对不对就看这个）
    可见像素的 RGB     必须完全一致（LZW / 调色板 / 透明索引对不对就看这个）
    alpha=0 区域的 RGB 不比对，单独统计（PIL 保留绿幕色，本实现保留 0，视觉无差异）
"""

import sys
from PIL import Image, ImageSequence

if len(sys.argv) < 3:
    print(__doc__)
    sys.exit(2)

REF_GIF = sys.argv[1]
OUT_DIR = sys.argv[2]
MINE = OUT_DIR + r"\frame_%04d.png"

im = Image.open(REF_GIF)
ref_frames = []
for frame in ImageSequence.Iterator(im):
    ref_frames.append(frame.convert("RGBA").copy())

N = int(sys.argv[3]) if len(sys.argv) > 3 else len(ref_frames)

print("reference frames:", len(ref_frames))

total_bad = 0
report = []
for i in range(min(N, len(ref_frames))):
    mine_path = MINE % i
    mine = Image.open(mine_path).convert("RGBA")
    ref = ref_frames[i]

    if mine.size != ref.size:
        report.append("frame %d SIZE MISMATCH ref=%s mine=%s" % (i, ref.size, mine.size))
        total_bad += 1
        continue

    rp = ref.load()
    mp = mine.load()
    alpha_bad = 0
    rgb_bad = 0
    rgb_bad_invisible = 0
    w, h = ref.size
    for y in range(h):
        for x in range(w):
            r = rp[x, y]
            m = mp[x, y]
            if r[3] != m[3]:
                alpha_bad += 1
                continue
            if r[3] == 0:
                # alpha 为 0 时 RGB 无意义，只统计用于信息展示
                if r[0] != m[0] or r[1] != m[1] or r[2] != m[2]:
                    rgb_bad_invisible += 1
                continue
            if r[0] != m[0] or r[1] != m[1] or r[2] != m[2]:
                rgb_bad += 1

    status = "OK" if (alpha_bad == 0 and rgb_bad == 0) else "MISMATCH"
    if status != "OK":
        total_bad += 1
    report.append(
        "frame %2d %-8s alphaDiff=%-6d rgbDiff(visible)=%-6d rgbDiff(alpha0,ignored)=%d"
        % (i, status, alpha_bad, rgb_bad, rgb_bad_invisible)
    )

for line in report:
    print(line)

print("-" * 70)
if total_bad == 0:
    print("RESULT: ALL FRAMES MATCH (alpha + visible RGB)")
else:
    print("RESULT: %d frame(s) have visible differences" % total_bad)
