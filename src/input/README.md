# input/ — 输入监听（M3，已实现"输入模式"）

给桌宠一个"感知你在干嘛"的入口。目前只有一个能力：**全局键盘监听**（输入模式）。

## 实际实现

| 文件 | 说明 |
|---|---|
| `KeyboardMonitor.h/.cpp` | 全局键盘监听。Windows 走 `SetWindowsHookEx(WH_KEYBOARD_LL)`，其它平台空实现 |

## ⚠️ 这是全项目唯一一个"挂钩式"能力，改动前请先读完

1. **默认关闭**：只有用户在菜单里显式打开「输入模式」才安装钩子。
2. **不记录按键内容**：只把按键的**虚拟键码（VK）**通过 `keyEvent(vk, down)` 交给上层；
   **不落盘、不打印日志、不进设置项**。以后要做"显示按键字符"时，这条约束要重新评估。
3. **杀软误报会明显加重**：本项目的「被报毒」说明见根目录 README，接了这个能力后那段必须同步更新
   （原来写的"无注入、无挂钩"不再成立）。
4. **必须在主线程调用 `start()`**：低层钩子要求安装它的线程有消息循环，Qt 主线程满足。
5. 钩子回调里**不直接 emit**，改成 `QMetaObject::invokeMethod(..., QueuedConnection)` 投递到事件循环，
   避免在消息泵内部执行业务逻辑。

## 上层怎么用

```cpp
auto* mon = new KeyboardMonitor(this);
connect(mon, &KeyboardMonitor::keyEvent, this, &PetController::onKeyEvent);
mon->start();     // 失败会返回 false，务必把原因提示给用户
```

触发频率由上层做节流（`PetController` 里的 `kInputCooldownMs`），
避免打字太快时动作动画狂闪。

## 后续可以接的

- 鼠标活动监听（同样的"是否挂钩"取舍）
- 空闲检测（`GetLastInputInfo`，**不是钩子**，误报风险低，优先做这个）
- 剪贴板 / 通知（需要别的权限模型，另说）
