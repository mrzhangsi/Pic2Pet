#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QImage>

#include "MediaLoader.h"

namespace {

/**
 * 把素材的每一帧导出成 PNG，用于和参考实现（PIL）做逐像素比对。
 *
 * 这是 APNG / GIF 合成逻辑唯一的自动化验证手段——
 * disposal 处理错一点，肉眼要盯很久才发现，而像素比对一秒出结果。
 */
int dump(const QString& path, const QString& outDir) {
    const pic2pet::MediaResult r = pic2pet::MediaLoader::loadPath(path);
    if (!r.ok) {
        qWarning().noquote() << "LOAD FAILED:" << r.error;
        return 1;
    }

    const pic2pet::FrameSequence& seq = r.seq;
    QDir dir(outDir);
    if (!dir.mkpath(QStringLiteral("."))) {
        qWarning().noquote() << "不能创建输出目录：" << outDir;
        return 1;
    }

    for (int i = 0; i < seq.frames.size(); ++i) {
        const QByteArray& bits = seq.frames.at(i);
        QImage img(reinterpret_cast<const uchar*>(bits.constData()), seq.width, seq.height,
                   QImage::Format_RGBA8888);
        const QString name = QStringLiteral("frame_%1.png").arg(i, 4, 10, QLatin1Char('0'));
        if (!img.copy().save(dir.filePath(name), "PNG")) {
            qWarning().noquote() << "写帧失败：" << name;
            return 1;
        }
    }

    qInfo().noquote() << QStringLiteral("OK source=%1 size=%2x%3 frames=%4 total=%5ms bytes=%6")
                             .arg(seq.sourceName.isEmpty() ? QStringLiteral("-") : seq.sourceName)
                             .arg(seq.width)
                             .arg(seq.height)
                             .arg(seq.count())
                             .arg(qint64(seq.totalDuration * 1000.0))
                             .arg(seq.bytes());

    QStringList delays;
    for (double d : seq.delays) delays << QString::number(qint64(d * 1000.0));
    qInfo().noquote() << "DELAY_MS:" << delays.join(QLatin1Char(','));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("pic2pet-madiadump"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("导出素材每一帧为 PNG，用于和参考实现比对"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("input"), QStringLiteral("素材文件或目录"));
    parser.addPositionalArgument(QStringLiteral("outdir"), QStringLiteral("PNG 输出目录"));
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.size() < 2) {
        parser.showHelp(1);
        return 1;
    }
    return dump(positional.at(0), positional.at(1));
}
