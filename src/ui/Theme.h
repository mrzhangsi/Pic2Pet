#pragma once

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;

namespace pic2pet::theme {

/**
 * 设计令牌（只做浅色，2026-09-23 决定）。
 *
 * 全部集中在这里，**不要在别处硬编码颜色** —— 以后想调风格（或者做深色模式）
 * 只需要动这一个文件，不用去每个控件里翻。
 */
inline const QColor WindowBg(0xF5, 0xF5, 0xF7);    // 窗口底（Apple 官网灰）
inline const QColor SidebarBg(0xEC, 0xEC, 0xEE);   // 侧边栏
inline const QColor ContentBg(0xFF, 0xFF, 0xFF);   // 内容区
inline const QColor GroupBg(0xF5, 0xF5, 0xF7);     // 分组卡背景
inline const QColor Separator(0xE5, 0xE5, 0xEA);   // 行分隔线
inline const QColor Border(0xD2, 0xD2, 0xD7);      // 描边

inline const QColor Accent(0x00, 0x7A, 0xFF);      // 主强调色（iOS 蓝）
inline const QColor SwitchOn(0x34, 0xC7, 0x59);    // 开关打开（iOS 绿）
inline const QColor SegmentBg(0xE9, 0xE9, 0xEB);   // 分段控件底
inline const QColor TrackBg(0xC7, 0xC7, 0xCC);     // 滑块轨道

inline const QColor TextPrimary(0x1D, 0x1D, 0x1F);
inline const QColor TextSecond(0x86, 0x86, 0x8B);
inline const QColor TextThird(0xC7, 0xC7, 0xCC);
inline const QColor MenuText(0x3C, 0x3C, 0x43);

/** 界面字体：按 macOS / Windows 中易读的顺序挑，取系统里第一个存在的 */
QFont uiFont(int pointDelta = 0);

/**
 * 全局样式表。
 * 只写"共用"的部分 —— 开关、分段控件、侧栏项这些为了做得像苹果是**自绘**的，
 * 不走 QSS（Qt 原生控件那套边框/渐变怎么调都不像）。
 */
QString styleSheet();

/**
 * 菜单样式（右键菜单 / 托盘菜单）。
 * 菜单是**顶层窗口**，不会从设置窗口继承样式，所以必须单独给它一份。
 */
QString menuStyleSheet();

/** 给整个 QApplication 套上字体与样式（main() 里调一次） */
void applyAppStyle(QApplication* app);

} // namespace pic2pet::theme
