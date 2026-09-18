#include "PetWindow.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "PetRenderer.h"

namespace pic2pet {

PetWindow::PetWindow(int size, QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint      // 无边框
                   | Qt::WindowStaysOnTopHint   // 置顶
                   | Qt::Tool);                 // 不进任务栏 / 不抢 Alt+Tab

    setAttribute(Qt::WA_TranslucentBackground, true);   // 关键：窗口整体透明
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);   // 显示时不抢焦点
    setMouseTracking(true);
    setAcceptDrops(true);                       // 支持把素材文件拖到角色上
    setWindowTitle(QStringLiteral("Pic2Pet"));

    renderer_ = new PetRenderer(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(renderer_);

    resize(size, size);
    updateInteractiveRect();
}

void PetWindow::setSpriteRect(const QRect& r) {
    spriteRect_ = r;
    updateInteractiveRect();
}

void PetWindow::updateInteractiveRect() {
    // 有精灵实际绘制区域就按它算，否则退回"窗口中心 70%"
    // （分级扩容下窗口可能远大于精灵，不跟着精灵走的话透明区会吃掉鼠标）
    const QRect base = spriteRect_.isValid() ? spriteRect_ : QRect(0, 0, width(), height());
    interactive_ = QRect(base.x() + int(base.width() * 0.15),
                         base.y() + int(base.height() * 0.15),
                         int(base.width() * 0.70),
                         int(base.height() * 0.70));
}

void PetWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        emit dragStarted();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        move(event->globalPosition().toPoint() - dragOffset_);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging_ && event->button() == Qt::LeftButton) {
        dragging_ = false;
        emit dragEnded();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PetWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateInteractiveRect();
}

void PetWindow::contextMenuEvent(QContextMenuEvent* event) {
    emit contextMenuRequested(event->globalPos());
    event->accept();
}

void PetWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() == nullptr) return;
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void PetWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (mime == nullptr || !mime->hasUrls()) {
        QWidget::dropEvent(event);
        return;
    }
    const QList<QUrl> urls = mime->urls();
    if (urls.isEmpty()) return;

    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) return;

    emit fileDropped(path);
    event->acceptProposedAction();
}

void PetWindow::wheelEvent(QWheelEvent* event) {
    // 只有光标落在角色上（即当前未穿透）时才收得到滚轮事件，
    // 透明区域会直接穿给下面的窗口，不需要在这里额外判断
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    // 一格 = 120，但高分屏触摸板/自由滚轮可能是很小的数值：
    // 攒够一定残量才走一格，否则一次滑动会触发几十次缩放，看起来就是在抖。
    wheelAccum_ += delta;
    int steps = wheelAccum_ / 120;
    if (steps != 0) {
        wheelAccum_ -= steps * 120;
    } else {
        if (qAbs(wheelAccum_) < 60) {
            event->accept();
            return;                       // 还没攒够一格，先不动
        }
        steps = wheelAccum_ > 0 ? 1 : -1;
        wheelAccum_ = 0;
    }

    emit wheelZoomed(steps);
    event->accept();
}

} // namespace pic2pet
