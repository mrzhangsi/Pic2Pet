# media/ — 素材解码（M1，已实现）

把 GIF / APNG / 静态图 / PNG 序列统一解码为 `FrameSequence`（RGBA8 帧数组 + 延迟数组）。

## 实际实现

| 文件 | 说明 |
|---|---|
| `FrameSequence.h/.cpp` | 帧容器 + `frameIndexAt(t)` + 帧内存统计 + `MediaResult` |
| `CanvasCompositor.h/.cpp` | blend / dispose 合成器，**GIF 与 APNG 共用一套** |
| `GifDecoder.h/.cpp` | 自研 GIF89a：LZW（变长码 + KWKwK）、交织、局部调色板、disposal |
| `ApngDecoder.h/.cpp` | 自研 chunk 分割（acTL/fcTL/fdAT）+ 拼帧成独立 PNG + 手写 CRC32 → `QImage` |
| `MediaLoader.h/.cpp` | 按魔数嗅探分发；目录序列（自然排序）；内存超额自动降采样 |

**M1 不引入任何第三方库**（技术方案 12.1）：GIF 完全自研；APNG 只做 chunk 层，
IDAT 的 inflate / 滤波还原交给 Qt 内置的 PNG 解码器；WebP 动图延后。

## 铁律

1. **加载时一次性全解码**，播放期零解码
2. 调度器在 `idx == lastFrameIndex` 时直接返回，不重绘（这是待机 0% CPU 的根本）
3. 解码层不依赖 Qt 平台相关的图像插件，保证三平台行为一致

## 验证手段

```bat
:: 导出每一帧为 PNG
build\tools\mediadump\pic2pet_mediadump.exe test.gif temp\out
```

再用 `tools\compare_frames.py` 与 PIL 的合成结果做逐像素比对。
`test.gif`（240x240 / 24 帧 / 全部 disposal=2）已实测 **24/24 帧 alpha 与可见 RGB 零差异**。

> alpha=0 区域的颜色值两边可能不同（PIL 保留绿幕色，本实现保留黑色），
> 因 alpha 为 0 故视觉无差异；比对脚本对此单独统计、不计入失败。

## 待补测试

- disposal=3（恢复备份）素材 —— 代码已实现，但手上没有素材覆盖
- APNG 素材 —— 代码已实现，**完全未测**
- 交织（interlace）GIF —— 代码已实现，未测

参考：`参考项目/desktop-pet2/DesktopPet.Windows/Playback/`（FrameSequence.h、AnimationPlayer.cpp、GIFDecoder.cpp）
