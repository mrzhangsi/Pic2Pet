#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QString>

#include "FrameSequence.h"

namespace pic2pet {

/**
 * 参数化变换 —— "动态效果"各通道叠加出来的结果（呼吸 / 打字回弹 / 拖拽倾斜）。
 *
 * 这是**不用 Live2D 也能做出跟手效果**的关键：Live2D 的本质是"可叠加的参数通道"，
 * 而不是模型格式。这里我们没有网格变形，只有刚体变换，但通道之间同样互相独立、可叠加。
 */
struct PetTransform {
    double dx = 0.0;          // 归一化位移（NDC：1.0 = 半个视口）
    double dy = 0.0;
    double zoomX = 1.0;       // 通道缩放（squash）
    double zoomY = 1.0;
    double rotateDeg = 0.0;   // 绕精灵**底边中点**旋转（像"身子歪"而不是"整体转"）

    bool isIdentity() const {
        return dx == 0.0 && dy == 0.0 && zoomX == 1.0 && zoomY == 1.0 && rotateDeg == 0.0;
    }
};

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

    /**
     * 实际上传到 GPU 的最后一帧，-1 表示还没上传过 */
    int uploadedFrame() const { return uploadedFrame_; }

    /**
     * 精灵在视口内的缩放（1.0,1.0 = 铺满窗口），双轴分开传。
     * 用途：窗口按"待机动作"的宽高比固定预留，而其它动作的宽高比可能不同，
     * 双轴缩放才能让每段动作都等比铺进同一个窗口而不变形。取值 (0, 1]。
     */
    void setSpriteScale(float sx, float sy);
    float spriteScaleX() const { return scaleX_; }
    float spriteScaleY() const { return scaleY_; }

    /** 设置动态通道的变换。与 spriteScale 是**两层**：前者是素材铺进窗口的等比缩放，
     *  后者是每帧变化的动作变换，两者在着色器里相乘。 */
    void setTransform(const PetTransform& t);
    const PetTransform& transform() const { return transform_; }

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
    float scaleX_ = 1.0f;        // 精灵在视口内的相对缩放，(0,1]，双轴
    float scaleY_ = 1.0f;
    PetTransform transform_;
};

} // namespace pic2pet
