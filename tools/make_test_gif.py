"""生成一个用于回归测试 / 运行时验证的透明动画 GIF。

- 240x240，24 帧，每帧 100ms（≈10fps）
- 背景透明（alpha=0），disposal=2（每帧后恢复为透明背景）
- 一个红色小球从左到右移动，左上角一个固定蓝色方块（验证 dispose/blend）
"""
import os
from PIL import Image, ImageDraw

W = 240
N = 24
DURATION = 100  # ms

frames = []
for i in range(N):
    img = Image.new('RGBA', (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # 从左到右移动的红球
    cx = int(30 + i * ((W - 120) / (N - 1)))
    d.ellipse([cx, 80, cx + 80, 160], fill=(255, 70, 70, 255))
    # 固定蓝色方块，测试 dispose/blend 不会把它擦掉（除非本帧重绘）
    d.rectangle([10, 10, 40, 40], fill=(70, 120, 255, 255))
    frames.append(img)

out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'test.gif')
frames[0].save(
    out,
    save_all=True,
    append_images=frames[1:],
    duration=DURATION,
    loop=0,
    disposal=2,
    transparency=0,
    optimize=False,
)
print('wrote', out, 'size=', frames[0].size, 'frames=', len(frames))

# 自检：重新打开确认透明与帧数
im = Image.open(out)
n = getattr(im, 'n_frames', 1)
print('verify n_frames =', n, ' transparency =', im.info.get('transparency'))
im.seek(0)
rgba = im.convert('RGBA')
print('verify corner alpha (should be 0) =', rgba.getpixel((5, 5))[3])
