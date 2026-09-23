#include "Theme.h"

#include <QApplication>

namespace pic2pet::theme {

QFont uiFont(int pointDelta) {
    // 注意：**不要拿这个去 setFont 整个 QApplication** —— 会 +52MB，见 applyAppStyle 的说明。
    // 它只适合给某个单独控件用（比如需要比默认大一号的标题）。
    QFont f;
    f.setPixelSize(13 + pointDelta);
    return f;
}

QString styleSheet() {
    // 命名约定：pageTitle / pageSubtitle / sectionTitle / card / rowTitle / rowSub / rowSubDim
    //           按钮 id：primary（实心蓝）/ ghost（白底描边）/ link（纯文字蓝）
    return QStringLiteral(R"(
QWidget { color: #1D1D1F; font-size: 13px; }

QWidget#settingsRoot { background: #FFFFFF; }
QWidget#sidebar      { background: #ECECEE; }

QLabel#pageTitle     { font-size: 17px; font-weight: 500; }
QLabel#pageSubtitle  { color: #86868B; font-size: 12px; }
QLabel#sectionTitle  { color: #86868B; font-size: 12px; }
QLabel#rowTitle      { font-size: 13px; }
QLabel#rowSub        { color: #86868B; font-size: 12px; }
QLabel#rowSubDim     { color: #C7C7CC; font-size: 12px; }
QLabel#hint          { color: #86868B; font-size: 12px; }

QFrame#card { background: #F5F5F7; border-radius: 10px; }
QFrame#sep  { background: #E5E5EA; max-height: 1px; border: none; }

QPushButton#primary {
    color: #FFFFFF; background: #007AFF; border: none; border-radius: 7px;
    padding: 6px 16px; font-size: 12px; font-weight: 500;
}
QPushButton#primary:hover   { background: #0A84FF; }
QPushButton#primary:pressed { background: #0060DF; }
QPushButton#primary:disabled { background: #D2D2D7; color: #FFFFFF; }

QPushButton#ghost {
    color: #1D1D1F; background: #FFFFFF; border: 1px solid #D2D2D7;
    border-radius: 7px; padding: 6px 14px; font-size: 12px;
}
QPushButton#ghost:hover   { background: #F5F5F7; }
QPushButton#ghost:pressed { background: #ECECEE; }

QPushButton#link {
    color: #007AFF; background: transparent; border: none;
    padding: 3px 9px; font-size: 12px; border-radius: 6px;
}
QPushButton#link:hover    { color: #0A84FF; background: rgba(0, 122, 255, 0.10); }
QPushButton#link:pressed  { background: rgba(0, 122, 255, 0.18); }
QPushButton#link:disabled { color: #C7C7CC; background: transparent; }

QSlider::groove:horizontal   { height: 4px; background: #C7C7CC; border-radius: 2px; }
QSlider::sub-page:horizontal { height: 4px; background: #007AFF; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 16px; height: 16px; margin: -6px 0; border-radius: 8px;
    background: #FFFFFF; border: 1px solid #C7C7CC;
}
QSlider::handle:horizontal:hover { border-color: #007AFF; }

/* 数字框做成"干净的输入框"：文字居中，**不要那对 Windows 老式上下箭头**。
   原因有两个：① 箭头会压在文字上（给足 padding 也只能缓解）；
   ② 那种立体小箭头本身就很旧，跟这套苹果风不搭。
   改成纯输入框 + 滚轮调整，宽度也省下来了。 */
QSpinBox {
    background: #FFFFFF; border: 1px solid #D2D2D7; border-radius: 6px;
    padding: 4px 8px; font-size: 12px; selection-background-color: #007AFF;
}
QSpinBox::up-button, QSpinBox::down-button {
    width: 0px; height: 0px; border: none; background: transparent;
}
QSpinBox:focus { border-color: #007AFF; }
/* 必须显式写 :disabled —— 一旦给控件设了 QSS，Qt 就不再按调色板画禁用态，
   不写这条的话"禁用了"和"能点"长得一模一样（动作页的停留框踩过） */
QSpinBox:disabled {
    background: #F5F5F7; color: #C7C7CC; border-color: #E5E5EA;
}

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

QToolTip { background: #1D1D1F; color: #FFFFFF; border: none; padding: 4px 8px; font-size: 12px; }
)");
}

QString menuStyleSheet() {
    // 右键菜单 / 托盘菜单单独用这一份（菜单是顶层窗口，不会从设置窗口继承样式）
    return QStringLiteral(R"(
QMenu { background: #FFFFFF; border: 1px solid #D2D2D7; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 5px 26px 5px 12px; border-radius: 5px; font-size: 13px; color: #1D1D1F; }
QMenu::item:selected { background: #007AFF; color: #FFFFFF; }
QMenu::item:disabled { color: #C7C7CC; }
QMenu::separator { height: 1px; background: #E5E5EA; margin: 4px 8px; }
QMenu::indicator { width: 14px; height: 14px; }
)");
}

void applyAppStyle(QApplication* app) {
    // 有意**什么都不做**，两个原因都是量出来的：

    // 1) 不设应用字体：实测只要调用 setFont()，常驻内存就 +52MB ——
    //    而且**跟挑哪个字体无关**（试过 6 项回退链、也试过只给 Segoe UI，都是 +52MB），
    //    所以不是"多套字体被加载"，而是"一旦显式设字体，Qt 就会把字体库整块拉起来"。
    //    代价太大，而我们其实不需要它：Windows 的系统默认字体本来就是 Segoe UI，
    //    macOS/Linux 也会自己给对平台字体 —— **让平台自己选才是正确做法**。
    //    字号在 QSS 里逐个控件指定（13px / 17px …），观感不受影响。
    // 2) 不设全局 styleSheet：桌宠是"透明 + 无边框 + OpenGL"窗口，
    //    全局套 QSS 会让 QStyleSheetStyle 接管所有控件，容易给透明窗口糊上背景。
    //    样式只挂在设置窗口上（它自己 setStyleSheet），菜单单独用 menuStyleSheet()。
    Q_UNUSED(app);
}

} // namespace pic2pet::theme
