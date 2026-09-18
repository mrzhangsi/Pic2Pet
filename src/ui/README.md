# ui/ — Qt Widgets 界面（M3）

用 Qt Widgets（不是 QML）：设置面板是表单式的，Widgets 更省内存、启动更快、二进制更小。

- `SettingsPanel` — 素材 / 缩放 / 透明度 / 速度 / 翻转 / 置顶 / 锁定 / 活动档位
- `ModelPanel` — 模型导入（PSD 拖入）、切换、删除、装配警告展示
- `TrayMenu` — 托盘右键菜单（M0 已在 PetController 里做了最小版）
- 外观用 QSS 处理圆角与配色

持久化走 `QSettings`（Ini 格式，`QStandardPaths::AppLocalDataLocation`），三平台统一。
