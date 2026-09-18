#pragma once

#include <QPoint>
#include <QRect>
#include <QWidget>

class QDragEnterEvent;
class QDropEvent;
class QWheelEvent;

namespace pic2pet {

class PetRenderer;

/**
 * 桌宠主窗口：无边框、透明、置顶、不抢焦点、不进任务栏。
 *
 * M0 只负责四件事：
 *   1. 承载 PetRenderer（验证透明合成）
 *   2. 提供命中区域（供穿透调度使用）
 *   3. 支持拖拽移动（验证穿透关闭时能否正常收到鼠标）
 *   4. 转发右键菜单请求
 */
class PetWindow : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(int size, QWidget* parent = nullptr);

    PetRenderer* renderer() const { return renderer_; }

    /** 可交互区域（逻辑坐标，相对窗口）；M2 会由角色包围盒自动生成 */
    QRect interactiveRect() const { return interactive_; }

signals:
    void dragStarted();
    void dragEnded();
    void contextMenuRequested(const QPoint& globalPos);
    void fileDropped(const QString& path);
    /** 滚轮缩放：steps > 0 放大，< 0 缩小（每格 = angleDelta/120） */
    void wheelZoomed(int steps);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateInteractiveRect();

    PetRenderer* renderer_ = nullptr;
    QRect interactive_;
    bool dragging_ = false;
    QPoint dragOffset_;
};

} // namespace pic2pet
