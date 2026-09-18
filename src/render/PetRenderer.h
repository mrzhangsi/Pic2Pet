#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QString>

#include "FrameSequence.h"

namespace pic2pet {

/**
 * 渲染后端： Qt::WA_TranslucentBackground 的窗口里用原生 OpenGL 绘制。
 *
 * 选型理由（技术方案 3.2）：用原生 OpenGL 而不是 Qt Quick，是因为
 * M2 的虹膜裁剪依赖 stencil buffer，而 Scene Graph 不暴露 stencil。
 *
 * M0 模式：程序化的圆盘 + 两只旋转眼睛，用来验证透明合成与动画流畅度。
 * M1 模式：精灵播放（setSprite + showFrame），真正的省 CPU 逻辑在这里——
 *          「帧序号没变就一次 GL 调用都不发」，这是 M1 性能达标的根。
 */
class PetRenderer : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit PetRenderer(QWidget* parent = nullptr);
    ~PetRenderer() override;

    // ---- M0 程序化模式 ----
    void setPhase(double phase);       // 0..2π 循环，由外部定时器驱动
    double phase() const { return phase_; }

    // ---- M1 精灵模式 ----
    void setSprite(const FrameSequence& seq);
    void clearSprite();
    bool hasSprite() const { return spriteMode_; }
    int spriteWidth() const { return spriteMode_ ? sprite_.width : 0; }
    int spriteHeight() const { return spriteMode_ ? sprite_.height : 0; }

    /**
     * 请求显示第 index 帧。
     * 与上一次请求的帧相同则**直接返回**，不触发 update()，也不做任何 GL 调用。
     */
    void showFrame(int index);

    /** 实际上传到 GPU 的最后一帧，-1 表示还没上传过 */
    int uploadedFrame() const { return uploadedFrame_; }

    int frameCount() const { return frames_; }
    QString glInfo() const { return glInfo_; }
    bool glReady() const { return ready_; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void ensureSpriteTexture();
    void uploadCurrentFrame();
    void releaseSpriteTexture();

    QOpenGLShaderProgram program_;        // M0 圆盘
    QOpenGLShaderProgram spriteProgram_;  // M1 精灵
    QOpenGLBuffer vbo_;
    QOpenGLBuffer spriteVbo_;             // 位置 + 纹理坐标，interleaved

    double phase_ = 0.0;
    int frames_ = 0;
    bool ready_ = false;
    QString glInfo_;

    FrameSequence sprite_;
    bool spriteMode_ = false;
    int pendingFrame_ = -1;      // 待显示帧
    int uploadedFrame_ = -1;     // 已上传 GPU 的帧
    GLuint texture_ = 0;
    bool textureAllocated_ = false;
};

} // namespace pic2pet
