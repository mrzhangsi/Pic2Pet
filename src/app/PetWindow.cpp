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

namespace {

// 按下后移动超过这个距离才算拖拽，否则松开时算"单击"
constexpr int kDragThresholdPx = 4;

} // namespace

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
        // 按下时先**不算**拖拽：等光标移动超过阈值才升级为拖拽，否则松开算单击。
        // （原来一按下就进拖拽，导致"点击动作"永远没机会触发。）
        pressed_ = true;
        dragging_ = false;
        suppressClick_ = false;
        pressPos_ = event->globalPosition().toPoint();
        dragOffset_ = pressPos_ - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent* event) {
    if (pressed_ || dragging_) {
        const QPoint gp = event->globalPosition().toPoint();

        if (!dragging_) {
            if ((gp - pressPos_).manhattanLength() < kDragThresholdPx) {
                event->accept();
                return;                     // 还在阈值内，仍当"点击候选"
            }
            dragging_ = true;
            // 用"升级为拖拽的那一刻"重新锚定，避免窗口跳一下
            dragOffset_ = gp - frameGeometry().topLeft();
            emit dragStarted();
        }

        move(gp - dragOffset_);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const bool wasDragging = dragging_;
    pressed_ = false;
    dragging_ = false;

    if (wasDragging) {
        emit dragEnded();
    } else if (!suppressClick_) {
        emit clicked();                     // 没移动 → 单击
    }
    suppressClick_ = false;
    event->accept();
}

void PetWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    // 双击的序列是 press / release / dblclick / release：
    // 第一次 release 已经发过 clicked()，这里再发 doubleClicked()；
    // 紧跟的第二次 release 要抑制掉，别再多打一次单击。
    suppressClick_ = true;
    emit doubleClicked();
    event->accept();
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
