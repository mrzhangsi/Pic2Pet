#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QSurfaceFormat>
#include <QTimer>

#include "ClickThrough.h"
#include "PetController.h"

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
    QApplication::setQuitOnLastWindowClosed(false);   // 托盘模式下关窗不退出

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Pic2Pet：透明桌宠，支持 GIF / APNG / 序列 / 静态图"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("media"),
                                 QStringLiteral("要加载的素材文件或目录（可省略，之后也能拖进来）"),
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
        QStringLiteral("启动时加载的素材文件或目录"),
        QStringLiteral("path"));
    parser.addOption(mediaOption);

    QCommandLineOption noTrayOption(QStringLiteral("no-tray"),
                                    QStringLiteral("不显示系统托盘图标"));
    parser.addOption(noTrayOption);

    QCommandLineOption grabOption(
        QStringLiteral("grab"),
        QStringLiteral("渲染一帧后保存为 PNG 并退出（用于自动化验证渲染路径）"),
        QStringLiteral("path"));
    parser.addOption(grabOption);

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

    qInfo().noquote() << QStringLiteral("[pet] 已启动：可把素材文件拖到角色上，或右键打开菜单");

    return app.exec();
}
