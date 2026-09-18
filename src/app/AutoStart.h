#pragma once

namespace pic2pet {

/**
 * 开机自启。三平台都写了实现，但**只有 Windows 实测通过**
 * （技术方案 12.0：macOS / Linux 只做代码层兼容）。
 *
 * 刻意不使用任何原生 API：
 *   Windows 走 QSettings 写 HKCU 的 Run 项（等价于操作注册表）
 *   macOS   写 ~/Library/LaunchAgents/<id>.plist
 *   Linux   写 ~/.config/autostart/<id>.desktop
 * 三者都是纯文件/配置读写，因此不存在编译不过的风险。
 */
class AutoStart {
public:
    static bool supported();
    static bool isEnabled();
    static bool setEnabled(bool on);
    static bool toggle() { return setEnabled(!isEnabled()); }
};

} // namespace pic2pet
