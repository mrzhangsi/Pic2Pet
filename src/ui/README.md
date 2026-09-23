# ui/ — 设计系统与可复用控件（浅色 / 苹果风）

**只放"长什么样"的东西，不放业务。** 它不认识 `Settings`、也不认识 `PetController` ——
设置窗口本身在 `src/app/SettingsWindow.{h,cpp}`（那里才需要读配置、调控制器）。

这样分层的理由：`Settings` 定义在 `app` 层，如果把设置窗口放在 `ui`，就会出现
`app → ui → app` 的依赖环。现在的关系是单向的：`app → ui`。

| 文件 | 说明 |
|---|---|
| `Theme.h/.cpp` | 设计令牌（颜色/字体）+ 全局 QSS。**所有颜色只在这里定义**，别处不要硬编码 |
| `Icons.h/.cpp` | 侧栏图标：自绘矢量描边（相片/播放/键盘/波形/明暗/齿轮） |
| `ToggleSwitch.h/.cpp` | iOS 风格开关（44×26 圆角轨道 + 滑动圆钮，带 140ms 动画） |
| `SegmentedControl.h/.cpp` | 分段控件（关/弱/中/强），选中块会滑动 |
| `SidebarItem.h/.cpp` | 侧栏项：图标 + 文字，选中态圆角蓝底 + 白字白图标 |

（原 M3 规划里的 `SettingsPanel` / `ModelPanel` 已合并成 `app/SettingsWindow`；
`ModelPanel` 那条 PSD 导入的线已按产品红线砍掉，见 `技术方案.md` 第 15 节。）

## 为什么这几个控件是自绘的

Qt 原生控件改不出苹果那套观感：`QCheckBox` 的方块勾选在苹果风里不成立，
QSS 也做不出"圆角轨道 + 会滑动的圆钮"；`QRadioButton` 排一排和分段控件差得远。
所以开关 / 分段 / 侧栏项这三个直接 `paintEvent` 画，其余的用 QSS 就够。

## 样式为什么不挂到 QApplication

`theme::applyAppStyle()` **只设字体、有意不设全局 styleSheet**：
桌宠是「透明 + 无边框 + OpenGL」窗口，全局套 QSS 会让 `QStyleSheetStyle` 接管所有控件，
容易给透明窗口糊上背景色。所以样式只挂在设置窗口上（它自己 `setStyleSheet`）；
右键菜单 / 托盘菜单另外用 `theme::menuStyleSheet()`（菜单是顶层窗口，不会从设置窗口继承）。

## 加新控件时的约定

- 颜色一律从 `theme::` 取，不要在控件里写死 `#RRGGBB`；
- 自绘控件记得 `setFocusPolicy(Qt::NoFocus)` —— 窗口里的控件不该抢键盘焦点，
  否则你一边打字一边动设置会把输入打断；
- 尺寸用 `setFixedSize` 定死，别让它们跟着布局抽动。
