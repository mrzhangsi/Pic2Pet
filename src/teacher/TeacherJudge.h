#pragma once

#include <optional>

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "TeacherConfig.h"

class QNetworkAccessManager;
class QNetworkRequest;

namespace pic2pet {

/** 一次模型判定的结果。失败时保守判为「学习中」——不冤枉用户、不打扰 */
struct TeacherJudgeResult {
    bool studying = true;
    QString comment;
    bool ok = false;
    QString error;          // no_api_key / budget_exceeded / bad_json / timeout / http_429 / ...
    qint64 promptTokens = 0;
    qint64 completionTokens = 0;
};

/**
 * L1 视觉模型判定：只处理 L0 拿不准的样本（OpenAI 兼容端点）。
 *
 * 成本控制的核心手段（全部对齐参考实现的实测结论）：
 *   1. 屏幕+摄像头合成一张图 → 图片 token 只算一份
 *   2. detail="low" → 服务端缩到 512x512 后再推理，约 159 tokens/张
 *   3. 提示词极简，不解释规则、不列举例外
 *   4. **关闭思考模式** → 实测开思考要 613 tokens，关掉只要 14，省 40 倍
 *   5. **单次会话调用预算硬护栏** → 超过 maxCalls 直接拒绝发请求
 *
 * 其他要点：
 *   - 网络异常 / 解析失败时保守判为学习中（调用方照常走状态机，不冤枉用户）
 *   - **绝不记录 API key、图片内容、请求体**；token 统计只记数字
 *   - 图片默认不落盘
 */
class TeacherJudge {
public:
    explicit TeacherJudge(const TeacherApi& api);

    /**
     * 判定一次。**阻塞**直到有结果或超时（只在监控线程调用）。
     * shouldStop 用于让退避 sleep 可被「停止监控」打断。
     */
    TeacherJudgeResult judge(const QByteArray& jpeg, const QString& goal,
                             const std::function<bool()>& shouldStop = nullptr);

    /** 逻辑调用次数（每 tick 最多 +1），用于报告与预算判断 */
    int totalCalls() const { return totalCalls_; }
    /** 真实 HTTP 请求数（含重试），用于排查实际发出多少请求 */
    int requestCount() const { return requestCount_; }
    qint64 totalPromptTokens() const { return totalPromptTokens_; }
    qint64 totalCompletionTokens() const { return totalCompletionTokens_; }
    qint64 lastPromptTokens() const { return lastPromptTokens_; }
    qint64 lastCompletionTokens() const { return lastCompletionTokens_; }

private:
    QNetworkRequest buildRequest() const;
    QByteArray buildPayload(const QString& goal, const QByteArray& jpeg) const;

    TeacherApi api_;
    QNetworkAccessManager* nam_ = nullptr;   // 懒创建：必须在监控线程里出生

    int totalCalls_ = 0;
    int requestCount_ = 0;
    qint64 totalPromptTokens_ = 0;
    qint64 totalCompletionTokens_ = 0;
    qint64 lastPromptTokens_ = 0;
    qint64 lastCompletionTokens_ = 0;
};

} // namespace pic2pet
