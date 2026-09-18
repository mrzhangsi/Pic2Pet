# Pic2Pet

跨平台桌面宠物（Qt 6 + C++20）。参考 Petra 的 2.5D 桌宠核心能力重写，
目标是在内存、启动速度、帧时间抖动上明显优于 WebView 方案，并原生支持 GIF / APNG / WebP。

整体方案见 **[技术方案.md](技术方案.md)**。

当前进度：**M1 —— GIF / APNG / 序列 / 静态图播放，可发布**（M0 spike 已完成验证）。

---

## 用法

```bat
scripts\build.bat                        REM 配置 + 编译
build\src\app\Pic2Pet.exe                REM 启动（自动恢复上次的素材与位置）
build\src\app\Pic2Pet.exe test.gif       REM 直接加载指定素材
build\src\app\Pic2Pet.exe --media <路径>  REM 同上，参数形式
build\src\app\Pic2Pet.exe --no-tray      REM 不显示托盘
```

运行中：**把素材文件直接拖到角色上**即可更换；右键角色或托盘图标打开菜单
（打开素材 / 最近素材 / 缩放 / 透明度 / 开机自启 / 始终置顶 / 显示隐藏 / 退出）。

支持：`.gif`（动）· `.png`（含 APNG 动图）· `.jpg` `.bmp` `.webp`（静态）
· **目录**（里面的图片按文件名自然排序组成序列）

控制台每秒输出一行：

```
[perf] timerFps=10 renderFps=10 cpu=1.6% mem=84.5 MB toggles=1 backend=WS_EX_TRANSPARENT (Win32) sprite=240x240/24f
```

`timerFps` 是**有效播放帧率**，`renderFps` 是 OpenGL 实际绘制帧数。
两者应约等于素材帧率——**若一个 10fps 的 GIF 显示 renderFps=60，说明短路失效了**。

### 打包

```bat
scripts\deploy.bat
```

产物 `dist\Pic2Pet.exe`，**约 29 MB / 11 个文件**，整个目录拷走即可运行。
脚本做了三件事：Release 编译（关闭控制台子系统）→ `windeployqt` 拷 Qt 运行时 → 精简。

精简掉的（46.8MB → 29.1MB）：`dxcompiler.dll` + `dxil.dll`（Qt 的 D3D11 RHI 着色器编译器，
约 15MB，我们只用 OpenGL）、`Qt6Network` / `Qt6Svg` 及它们带来的网络信息 / TLS / SVG 插件。

> `--no-compiler-runtime` 表示**不打包 VC++ 运行库**，目标机器需已安装
> VS 2015–2022 Redistributable；要自带运行库就去掉这个参数。

## 验证工具

```bat
:: 把素材每一帧导出成 PNG
build\tools\mediadump\pic2pet_mediadump.exe test.gif temp\out

:: 与 PIL 的合成结果逐像素比对（需要 Python + Pillow）
python tools\compare_frames.py test.gif temp\out 24
```

```bat
:: 离屏抓取一帧并退出（不依赖窗口是否可见，适合自动化）
build\src\app\Pic2Pet.exe --media test.gif --grab temp\frame.png
```

## 环境要求

| 组件 | 版本 | 状态 |
|---|---|---|
| Visual Studio 2022（MSVC v143 + Windows SDK 10.0.26100） | 14.44.35207 | ✅ |
| Qt 6（MSVC 2022 64-bit） | 6.11.2，装在 `D:\QT\6.11.2\msvc2022_64` | ✅ |
| CMake（独立安装） | 4.4.3，`C:\Program Files\CMake\bin` | ✅ |
| Ninja（winget 安装） | 1.13.2 | ✅ |
| vcpkg | — | **M1 已确认不需要**（GIF 自研、APNG 复用 Qt 的 PNG 解码） |

路径都写在 `scripts\build.bat` 顶部，换机器只改那几行。

## 构建

```bat
scripts\build.bat            REM 配置 + 编译
scripts\build.bat clean      REM 清空 build 目录（换生成器/换工具链前必做）
scripts\build.bat deploy     REM 编译后跑 windeployqt 拷贝 Qt 运行时
```

脚本会自动调 `vcvars64.bat` 注入 MSVC 环境，并**显式钉死 `CMAKE_CXX_COMPILER` 指向 cl.exe**，
避免 PATH 里的 MSYS2/MinGW g++ 被 CMake 误选。

## M0 验证清单（2026-09-18 已跑，Windows）

- [x] **透明合成**：✅ 圆盘边缘无黑边，桌面可见；拿到的是原生 desktop OpenGL 4.6（NVIDIA），未回落软件渲染
- [x] **穿透**：✅ 拖动正常，透明区域可穿透点击
- [x] **切换不闪烁**：⚠️ native(`WS_EX_TRANSPARENT`) 9 秒内仅 1 次 toggle，稳定；
      Qt(`WindowTransparentForInput`) 出现 1→2 抖动 → **Windows 默认锁 native 后端**
- [x] **开销**：✅ 内存目标放宽到 ≤80MB 基线（见技术方案第 5 节修订说明）；
      M1 实测静态图 CPU 0–1.6%、动画 1.6% 左右
- [x] **多屏 / DPI**：✅ dpr=1.50 下坐标与命中区正确；副屏未测
- [ ] **托盘与退出**：未测（均以 `--no-tray` 运行）

## M1 验证清单（2026-09-18 已跑，Windows）

- [x] **GIF 解码正确性**：✅ `test.gif` 24/24 帧与 PIL 参考实现**逐像素零差异**（disposal=2 全覆盖）
- [x] **三重短路**：✅ 静态图 `timerFps=0 renderFps=0`；10fps 的 GIF `renderFps=10`（不是 60）
- [x] **静态图 / 目录序列**：✅ 两条路径均跑通
- [x] **拖拽加载 + 配置持久化**：✅ 重启自动恢复上次素材与位置
- [x] **渲染路径**：✅ `--grab` 离屏抓取，透明区占比与内容包围盒均与源素材吻合
- [ ] **APNG**：代码已实现但**无素材，完全未测**
- [ ] **disposal=3 / 交织 GIF**：代码已实现，无素材覆盖
- [ ] **开机自启**：Windows 路径已实现，未实测；macOS/Linux 只保证编译

实测明细见 **[技术方案.md 第 11、13 节](技术方案.md)**。

Windows 上 `qInfo()` 默认不走控制台，看日志需设 `QT_FORCE_STDERR_LOGGING=1`。

## 目录结构

```
src/
├─ platform/   ClickThrough（Qt / Win32）、HitRegions、PerfProbe
├─ render/     PetRenderer（M0 程序化圆盘 + M1 精灵播放）
├─ app/        PetWindow、PetController、Settings、AutoStart、main
├─ media/      M1  GifDecoder / ApngDecoder / MediaLoader / CanvasCompositor
├─ psd/        M2  PSD 解析 + rigger 装配 + .petbin
├─ rig/        M2  RigParams / deform / 物理 / 表情 / 动作
├─ behavior/   M3  行为状态机
└─ ui/         M3  Qt Widgets 面板
tools/
└─ mediadump/  帧导出工具，用于解码正确性的逐像素回归
```

## 里程碑

| 阶段 | 内容 | 状态 |
|---|---|---|
| M0 | spike：透明窗口 + GL + 穿透 + 拖拽 | ✅ 已完成 |
| M1 | GIF/APNG/序列/静态图播放 + 托盘 + 设置 + 开机自启 | ✅ 已实现（APNG 待素材验证） |
| M2 | PSD 解析 + rigger + 2.5D 渲染 + .petbin | ⬜ |
| M3 | 行为状态机 + 表情/动作 + 多宠物 + 设置面板 | ⬜ |
| M4 | 多屏 DPI、三平台打包 | ⬜ |

## 许可

本项目代码待定。移植部分需保留上游署名：Petra (MIT)、Anime2.5DRig (MIT)、
ag-psd (MIT)、desktop-pet2 (MIT)。注意 desktop-pet1 为 GPL-3.0，未参考其任何代码。
Qt 6 采用 LGPLv3 时需动态链接并随附许可声明。

---

## 发版（GitHub Release，可直接下载的发行版）

仓库自带 GitHub Actions：打版本 tag 后**自动用 MSVC + Qt 编译、打包成 zip 挂到 Release**，
用户点开 Release 页面即可下载，且二进制可对照源码复现（可信）。

### 自动发版（推荐）

```bat
git tag v1.0.0
git push origin v1.0.0
```

推送 `v*` tag 即触发 `.github/workflows/release.yml`：编译 Release → `windeployqt` 部署 →
精简 → 压缩成 `Pic2Pet-<tag>.zip` → 自动创建同名 GitHub Release 并附上该 zip。
用户下载解压后双击 `Pic2Pet.exe` 即可运行（需目标机已装 VS 2015–2022 Redistributable）。

首次使用需在仓库 **Settings → Actions → General → Workflow permissions** 设为
*Read and write*，否则创建 Release 会失败。

### 手动发版（本地构建后上传）

```bat
scripts\deploy.bat                 REM 产出 dist\（≈29MB / 11 文件）
powershell -Command "Compress-Archive -Path dist\* -DestinationPath Pic2Pet-v1.0.0.zip"
```

然后在 GitHub 上 **Draft a new release**，把 `Pic2Pet-v1.0.0.zip` 作为附件上传。

> 说明：本机 `scripts\deploy.bat` 依赖被安全策略拦截的 `vcvars64.bat`，可改用
> `temp\deploy.ps1`（PowerShell 显式注入 MSVC 环境，等价替代）来本地打包。

### 关于「被报毒 / SmartScreen」

本程序**不含任何恶意行为**，但作为未签名 exe + 鼠标穿透(`WS_EX_TRANSPARENT`) +
可选开机自启的组合，**容易被 Windows Defender / SmartScreen 误报**。
已采取的措施：exe 内嵌应用清单声明**不申请管理员权限**(`asInvoker`)、DPI 感知、Win10/11 兼容
（见 `src/app/app.manifest`，由 `mt.exe` 在打包时写入）。
若仍被拦，最彻底的解决是**对 exe 做 Authenticode 代码签名**（需要代码签名证书）；
开源 + CI 可复现构建本身也能让用户自行核验二进制与源码一致。
