#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>

#include "ClickThrough.h"
#include "PetController.h"
#include "Theme.h"

namespace {

// 必须在 QApplication 构造之前设置默认 surface format。
// alpha 用于半透明窗口，stencil 为 M2 的虹膜裁剪预留。
void configureSurfaceFormat() {
    QSurfaceFormat fmt;
    fmt.setAlphaBufferSize(8);
    fmt.setStencilBufferSize(8);
    fmt.setDepthBufferSize(24);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);
}

} // namespace

int main(int argc, char** argv) {
    configureSurfaceFormat();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Pic2Pet"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.ico")));   // 来自 app.qrc
    QApplication::setQuitOnLastWindowClosed(false);   // 托盘模式下关窗不退出
    pic2pet::theme::applyAppStyle(&app);              // 界面字体（样式表只挂在设置窗口上）

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Pic2Pet：透明桌宠，支持 GIF / APNG / 静态图"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("media"),
                                 QStringLiteral("要加载的素材文件（可省略，之后也能拖进来）"),
                                 QStringLiteral("[media]"));

    QCommandLineOption backendOption(
        QStringList() << QStringLiteral("b") << QStringLiteral("backend"),
        QStringLiteral("鼠标穿透实现：qt 或 native（默认 native）"),
        QStringLiteral("name"), QStringLiteral("native"));
    parser.addOption(backendOption);

    QCommandLineOption sizeOption(
        QStringList() << QStringLiteral("s") << QStringLiteral("size"),
        QStringLiteral("窗口边长（默认 360）"),
        QStringLiteral("px"), QStringLiteral("360"));
    parser.addOption(sizeOption);

    QCommandLineOption mediaOption(
        QStringList() << QStringLiteral("m") << QStringLiteral("media"),
        QStringLiteral("启动时加载的素材文件"),
        QStringLiteral("path"));
    parser.addOption(mediaOption);

    QCommandLineOption noTrayOption(QStringLiteral("no-tray"),
                                    QStringLiteral("不显示系统托盘图标"));
    parser.addOption(noTrayOption);

    QCommandLineOption settingsOption(
        QStringLiteral("settings"),
        QStringLiteral("启动后直接打开设置窗口，可带页号 0~6（开发 / 截图验证用）"),
        QStringLiteral("page"), QStringLiteral("0"));
    parser.addOption(settingsOption);

    QCommandLineOption grabOption(
        QStringLiteral("grab"),
        QStringLiteral("渲染一帧后保存为 PNG 并退出（用于自动化验证渲染路径）"),
        QStringLiteral("path"));
    parser.addOption(grabOption);

    QCommandLineOption teacherOption(
        QStringLiteral("teacher"),
        QStringLiteral("启动后直接开启 AI 教官监控（用当前保存的配置，默认目标「学习」）"));
    parser.addOption(teacherOption);

    parser.process(app);

    const QString backendName = parser.value(backendOption);
    const auto backend = (backendName.compare(QStringLiteral("qt"), Qt::CaseInsensitive) == 0)
                             ? pic2pet::ClickThroughBackend::Qt
                             : pic2pet::ClickThroughBackend::Native;

    bool ok = false;
    const int size = parser.value(sizeOption).toInt(&ok);
    const int windowSize = (ok && size >= 64 && size <= 1024) ? size : 360;

    QString mediaPath = parser.value(mediaOption);
    if (mediaPath.isEmpty()) {
        const QStringList positional = parser.positionalArguments();
        if (!positional.isEmpty()) mediaPath = positional.first();
    }

    pic2pet::PetController controller(backend, windowSize, !parser.isSet(noTrayOption));
    controller.start(mediaPath);

    if (!mediaPath.isEmpty() && !controller.hasMedia()) {
        qWarning().noquote() << "[pet] 素材加载失败：" << controller.lastError();
    }

    qInfo().noquote() << controller.statusLine();

    if (parser.isSet(grabOption)) {
        const QString out = parser.value(grabOption);
        // 等一帧稳定后再抓，避免抓到还没上传纹理的空帧
        QTimer::singleShot(500, &app, [&controller, out] {
            const bool ok = controller.grabFramebuffer(out);
            qInfo().noquote() << QStringLiteral("GRAB %1 %2").arg(ok ? QStringLiteral("ok")
                                                                     : QStringLiteral("failed"))
                                     .arg(out);
            QCoreApplication::quit();
        });
        return app.exec();
    }

    if (parser.isSet(settingsOption)) {
        bool pageOk = false;
        // 支持 `--settings=6` 或 `--settings=6=C:\out\page.png`。
        // 带路径时开窗后离屏抓整页并退出 —— 用于自动化验证界面，
        // 绕开「全屏截图会截到压在窗口上面的其它程序」这个问题。
        const QString value = parser.value(settingsOption);
        const int colon = value.indexOf(QLatin1Char('='));
        const QString pagePart = colon >= 0 ? value.left(colon) : value;
        const QString grabPath = colon >= 0 ? value.mid(colon + 1) : QString();
        const int page = pagePart.toInt(&pageOk);

        controller.openSettings(pageOk ? page : 0);

        if (!grabPath.isEmpty()) {
            QTimer::singleShot(700, &app, [&controller, page, grabPath] {
                // 关掉已开的那扇，再离屏抓当前页 —— 抓整页内容而不是视口
                const bool ok = controller.grabSettingsPage(page, grabPath);
                qInfo().noquote() << QStringLiteral("GRAB-SETTINGS %1 %2")
                                         .arg(ok ? QStringLiteral("ok") : QStringLiteral("failed"))
                                         .arg(grabPath);
                QCoreApplication::quit();
            });
            return app.exec();
        }
    }

    if (parser.isSet(teacherOption)) {
        controller.startTeacher();
    }

    qInfo().noquote() << QStringLiteral("[pet] 已启动：可把素材文件拖到角色上，或右键打开菜单");

    return app.exec();
}
