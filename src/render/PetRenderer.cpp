#include "PetRenderer.h"

#include <QColor>
#include <QDebug>
#include <QOpenGLShader>
#include <QSurfaceFormat>

#include <cstring>

namespace pic2pet {

namespace {

// 不写 #version：同一份源码在「桌面 GLSL 1.10」和「GLSL ES 1.00」下都能编译，
// 从而同时兼容 Windows(ANGLE/desktop GL)、macOS、Linux。
// GL_ES 宏由 ES 编译器自动定义，用它保护 precision 限定符。
const char* kVertexShader = R"(
attribute vec2 aPos;
varying vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 vUv;
uniform float uPhase;
uniform vec3 uColor;
void main() {
    vec2 p = vUv * 2.0 - 1.0;
    float r = length(p);

    // 圆盘：外缘羽化，用来检查半透明边缘是否有黑边 / 锯齿
    float body = 1.0 - smoothstep(0.70, 0.80, r);
    if (body <= 0.002) discard;

    // 两只随相位旋转的眼睛：用来肉眼判断动画是否流畅、有无抖动
    float c = cos(uPhase);
    float s = sin(uPhase);
    vec2 q = vec2(p.x * c - p.y * s, p.x * s + p.y * c);
    float eyeL = 1.0 - smoothstep(0.09, 0.12, length(q - vec2(-0.26, -0.12)));
    float eyeR = 1.0 - smoothstep(0.09, 0.12, length(q - vec2( 0.26, -0.12)));

    vec3 col = mix(uColor, vec3(0.06, 0.06, 0.09), clamp(eyeL + eyeR, 0.0, 1.0));
    gl_FragColor = vec4(col, body);
}
)";

// M1 精灵着色器：只做纹理采样 + alpha 丢弃。
// 注意没有做 gamma / 颜色空间转换——素材本身就是 sRGB，这里原样输出即可。
const char* kSpriteVertexShader = R"(
attribute vec2 aPos;
attribute vec2 aTex;
varying vec2 vUv;
void main() {
    vUv = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kSpriteFragmentShader = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 vUv;
uniform sampler2D uTex;
void main() {
    vec4 texel = texture2D(uTex, vUv);
    if (texel.a <= 0.004) discard;
    gl_FragColor = texel;
}
)";

} // namespace

PetRenderer::PetRenderer(QWidget* parent)
    : QOpenGLWidget(parent),
      vbo_(QOpenGLBuffer::VertexBuffer),
      spriteVbo_(QOpenGLBuffer::VertexBuffer) {
    // 顶层窗口需要 Qt::WA_TranslucentBackground，这里再保证 GL surface 带 alpha。
    // stencil 是为 M2 的虹膜裁剪预留的（PsdRuntime 依赖 stencil 做模板测试）。
    QSurfaceFormat fmt = format();
    fmt.setAlphaBufferSize(8);
    fmt.setStencilBufferSize(8);
    fmt.setDepthBufferSize(24);
    setFormat(fmt);

    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_AlwaysStackOnTop, false);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

PetRenderer::~PetRenderer() {
    makeCurrent();
    releaseSpriteTexture();
    vbo_.destroy();
    spriteVbo_.destroy();
    doneCurrent();
}

void PetRenderer::setPhase(double phase) { phase_ = phase; }

void PetRenderer::setSprite(const FrameSequence& seq) {
    sprite_ = seq;
    spriteMode_ = seq.isValid();
    pendingFrame_ = spriteMode_ ? 0 : -1;
    // 强制下一次 paintGL 重新建纹理并上传第一帧
    uploadedFrame_ = -1;
    textureAllocated_ = false;
    if (spriteMode_) update();
}

void PetRenderer::clearSprite() {
    sprite_ = FrameSequence();
    spriteMode_ = false;
    pendingFrame_ = -1;
    uploadedFrame_ = -1;
    textureAllocated_ = false;
    update();
}

void PetRenderer::showFrame(int index) {
    if (!spriteMode_) return;
    if (index < 0) index = 0;
    if (sprite_.count() > 0) index = index % sprite_.count();

    // 三重短路里的第 2 条：帧没变就什么都别做
    if (index == pendingFrame_) return;

    pendingFrame_ = index;
    update();
}

void PetRenderer::ensureSpriteTexture() {
    if (textureAllocated_) return;
    if (!spriteMode_ || sprite_.width <= 0 || sprite_.height <= 0) return;

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 只分配一次纹���空间，之后每帧用 glTexSubImage2D 增量更新，
    // 避免为每一帧各建一张纹理导致显存翻倍（技术方案 12.2）
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sprite_.width, sprite_.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    textureAllocated_ = true;
}

void PetRenderer::uploadCurrentFrame() {
    if (!textureAllocated_) return;
    if (pendingFrame_ < 0 || pendingFrame_ >= sprite_.frames.size()) return;

    const QByteArray& frame = sprite_.frames.at(pendingFrame_);
    if (frame.size() < sprite_.width * sprite_.height * 4) return;

    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sprite_.width, sprite_.height,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    reinterpret_cast<const void*>(frame.constData()));

    uploadedFrame_ = pendingFrame_;
}

void PetRenderer::releaseSpriteTexture() {
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    textureAllocated_ = false;
}

void PetRenderer::initializeGL() {
    initializeOpenGLFunctions();

    const auto versionOf = [this](GLenum name) -> QString {
        const auto* raw = reinterpret_cast<const char*>(this->glGetString(name));
        return raw ? QString::fromLatin1(raw) : QStringLiteral("unknown");
    };
    glInfo_ = QStringLiteral("%1 / %2").arg(versionOf(GL_VERSION), versionOf(GL_RENDERER));

    if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)) {
        qWarning() << "[render] vertex shader:" << program_.log();
        return;
    }
    if (!program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)) {
        qWarning() << "[render] fragment shader:" << program_.log();
        return;
    }
    if (!program_.link()) {
        qWarning() << "[render] link:" << program_.log();
        return;
    }

    if (!spriteProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kSpriteVertexShader)) {
        qWarning() << "[render] sprite vertex shader:" << spriteProgram_.log();
        return;
    }
    if (!spriteProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kSpriteFragmentShader)) {
        qWarning() << "[render] sprite fragment shader:" << spriteProgram_.log();
        return;
    }
    if (!spriteProgram_.link()) {
        qWarning() << "[render] sprite link:" << spriteProgram_.log();
        return;
    }

    static const float quad[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };
    vbo_.create();
    vbo_.bind();
    vbo_.allocate(quad, sizeof(quad));

    // 精灵用的 interleaved 顶点：位置 + 纹理坐标。
    // 纹理坐标的 v 取反与否取决于素材行序——这里素材是自上而下，
    // 而 GL 的 t 轴自下而上，所以这里翻转一次。
    static const float spriteQuad[] = {
        -1.0f, -1.0f,   0.0f, 1.0f,
         1.0f, -1.0f,   1.0f, 1.0f,
        -1.0f,  1.0f,   0.0f, 0.0f,
         1.0f,  1.0f,   1.0f, 0.0f,
    };
    spriteVbo_.create();
    spriteVbo_.bind();
    spriteVbo_.allocate(spriteQuad, sizeof(spriteQuad));

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    ready_ = true;
}

void PetRenderer::resizeGL(int w, int h) {
    Q_UNUSED(w);
    Q_UNUSED(h);
    // 视口由 QOpenGLWidget 按 devicePixelRatio 自动设置，这里无需处理
}

void PetRenderer::paintGL() {
    ++frames_;

    if (!ready_) {
        // 着色器失败时清成洋红，方便一眼区分「渲染失败」与「透明」
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (spriteMode_) {
        ensureSpriteTexture();
        uploadCurrentFrame();
        if (!textureAllocated_) return;

        spriteProgram_.bind();
        spriteVbo_.bind();

        const int posLoc = spriteProgram_.attributeLocation("aPos");
        const int texLoc = spriteProgram_.attributeLocation("aTex");

        spriteProgram_.enableAttributeArray(posLoc);
        spriteProgram_.enableAttributeArray(texLoc);
        spriteProgram_.setAttributeBuffer(posLoc, GL_FLOAT, 0, 2, 4 * sizeof(float));
        spriteProgram_.setAttributeBuffer(texLoc, GL_FLOAT, 2 * sizeof(float), 2,
                                          4 * sizeof(float));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_);
        spriteProgram_.setUniformValue("uTex", 0);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        spriteProgram_.disableAttributeArray(posLoc);
        spriteProgram_.disableAttributeArray(texLoc);
        spriteProgram_.release();
        return;
    }

    // ---- M0 程序化模式（保留：没有素材时的占位，也方便回归对比）----
    program_.bind();
    vbo_.bind();

    const int loc = program_.attributeLocation("aPos");
    program_.enableAttributeArray(loc);
    program_.setAttributeBuffer(loc, GL_FLOAT, 0, 2, 0);
    program_.setUniformValue("uPhase", static_cast<float>(phase_));
    program_.setUniformValue("uColor", QColor(0x4C, 0x9A, 0xFF));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    program_.disableAttributeArray(loc);
    program_.release();
}

} // namespace pic2pet
