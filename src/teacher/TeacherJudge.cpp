#include "TeacherJudge.h"

#include "TeacherCapture.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

#include <algorithm>
#include <functional>

namespace pic2pet {

namespace {

// 极简提示词：不解释规则、不列举例外，只给输出格式。
// **必须用双引号**——开启 JSON Output 后模型会模仿提示词里的写法，
// 原来写单引号 {'s':0} 会让模型也回单引号，json.loads 解析不了。
// 判定规则要写清「什么不算学」——实测不写这条，模型会把低头玩手机判成在学。
const QString kPrompt =
    QStringLiteral(
        "图1=屏幕，图2=摄像头(可能无)。判断用户此刻是否在认真学习。\n"
        "不算：玩手机/刷视频/看小说/社交聊天/打游戏/发呆/离开。\n"
        "屏幕内容与目标一致、且姿态像在看资料才算。\n"
        "仅输出 json，格式：{\"s\": 0或1, \"c\": \"≤8字\"}");

/** 从模型输出里抠出 JSON 对象，容忍多余文字和 ```json 代码块 */
std::optional<QJsonObject> extractJson(const QString& text) {
    QString t = text.trimmed();
    if (t.isEmpty()) return std::nullopt;

    static const QRegularExpression kFenceOpen(QStringLiteral("^```(?:json)?\\s*"));
    static const QRegularExpression kFenceClose(QStringLiteral("\\s*```$"));
    t.remove(kFenceOpen);
    t.remove(kFenceClose);

    const auto tryParse = [](const QString& s) -> std::optional<QJsonObject> {
        const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
        if (!doc.isNull() && doc.isObject()) return doc.object();
        return std::nullopt;
    };

    if (auto obj = tryParse(t)) return obj;

    // 截取所有形如 {...} 的片段，按长度降序尝试
    QVector<QPair<int, int>> spans;   // (start, end)
    int depth = 0;
    int start = -1;
    for (int i = 0; i < t.size(); ++i) {
        const QChar ch = t.at(i);
        if (ch == QLatin1Char('{')) {
            if (depth == 0) start = i;
            ++depth;
        } else if (ch == QLatin1Char('}') && depth > 0) {
            --depth;
            if (depth == 0 && start >= 0) {
                spans.append({start, i + 1});
                start = -1;
            }
        }
    }
    std::stable_sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) {
        return (a.second - a.first) > (b.second - b.first);
    });
    for (const auto& span : spans) {
        if (auto obj = tryParse(t.mid(span.first, span.second - span.first))) return obj;
    }
    return std::nullopt;
}

/** 可中断的 sleep：每 100ms 检查一次停止标志 */
void interruptibleSleep(int ms, const std::function<bool()>& shouldStop) {
    const int step = 100;
    for (int waited = 0; waited < ms; waited += step) {
        if (shouldStop && shouldStop()) return;
        QThread::msleep(qMin(step, ms - waited));
    }
}

} // namespace

TeacherJudge::TeacherJudge(const TeacherApi& api) : api_(api) {}

QNetworkRequest TeacherJudge::buildRequest() const {
    // httpx 的 base_url + post("/chat/completions") 语义：拼到端点根路径。
    // DeepSeek 用 https://api.deepseek.com/chat/completions（根路径，无 /v1）；
    // Moonshot 等则填 https://api.moonshot.cn/v1。
    QString base = api_.baseUrl;
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    QNetworkRequest request(QUrl(base + QStringLiteral("/chat/completions")));
    request.setRawHeader("Authorization", "Bearer " + api_.apiKey.toUtf8());
    request.setRawHeader("Content-Type", "application/json");
    return request;
}

QByteArray TeacherJudge::buildPayload(const QString& goal, const QByteArray& jpeg) const {
    QJsonObject textPart;
    textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
    textPart.insert(QStringLiteral("text"), kPrompt + QStringLiteral("\n目标：") + goal);

    QJsonObject imageUrl;
    imageUrl.insert(QStringLiteral("url"), teacherToDataUrl(jpeg));
    imageUrl.insert(QStringLiteral("detail"), api_.imageDetail);
    QJsonObject imagePart;
    imagePart.insert(QStringLiteral("type"), QStringLiteral("image_url"));
    imagePart.insert(QStringLiteral("image_url"), imageUrl);

    QJsonArray content;
    content.append(textPart);
    content.append(imagePart);

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), content);

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), api_.model);
    payload.insert(QStringLiteral("messages"), QJsonArray{message});
    payload.insert(QStringLiteral("max_tokens"), api_.maxOutputTokens);
    payload.insert(QStringLiteral("temperature"), 0);
    payload.insert(QStringLiteral("stream"), false);

    // DeepSeek 专属：思考模式必须关。flash 默认开思考，思考链 token 计入
    // completion_tokens——实测 max_tokens=48 时 48 个 token 全被思考吃掉，
    // 正文一字未出。其他 OpenAI 兼容服务不认识这个字段，硬发会 400。
    if (api_.sendThinking()) {
        QJsonObject thinking;
        thinking.insert(QStringLiteral("type"), QStringLiteral("disabled"));
        payload.insert(QStringLiteral("thinking"), thinking);
    }
    // JSON Output（OpenAI 标准字段）。提示词已含 json 字样与格式样例。
    if (api_.jsonOutput) {
        QJsonObject rf;
        rf.insert(QStringLiteral("type"), QStringLiteral("json_object"));
        payload.insert(QStringLiteral("response_format"), rf);
    }
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

TeacherJudgeResult TeacherJudge::judge(const QByteArray& jpeg, const QString& goal,
                                       const std::function<bool()>& shouldStop) {
    TeacherJudgeResult v;

    if (api_.apiKey.isEmpty()) {
        v.ok = false;
        v.error = QStringLiteral("no_api_key");
        v.comment = QStringLiteral("未配置 API key，跳过判定");
        v.studying = true;   // 保守：不动状态、不打扰
        return v;
    }

    // 预算护栏：按「逻辑调用次数」算。超过上限就不再发请求，
    // 避免调参数时反复试导致失控（实测一次调参就发了 118 次）。
    if (totalCalls_ >= api_.maxCalls) {
        v.ok = false;
        v.error = QStringLiteral("budget_exceeded");
        v.comment = QStringLiteral("已达调用预算上限，停止调用模型");
        v.studying = true;
        return v;
    }
    ++totalCalls_;

    // QNetworkAccessManager 有线程亲和性：必须在监控线程里创建。
    // 懒创建 + 常驻复用（keep-alive），跨 tick 不重建连接。
    if (!nam_) nam_ = new QNetworkAccessManager();

    const QNetworkRequest request = buildRequest();
    const QByteArray payload = buildPayload(goal, jpeg);

    QString lastErr;
    for (int attempt = 0; attempt <= api_.maxRetries; ++attempt) {
        // 用户点了停止：不等重试，直接收尾
        if (shouldStop && shouldStop()) break;

        // 每次真的发出请求就计数，包含失败与重试——失败恰恰是最容易
        // 反复触发、也最该被拦住的情况。
        ++requestCount_;

        auto* reply = nam_->post(request, payload);

        // 同步等待：嵌套事件循环。超时 / finished / 停止监控 都会把 loop 唤醒。
        QEventLoop loop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timeoutTimer.start(api_.timeoutMs);
        // 每 200ms 检查一次「停止监控」：点了停止不该让用户干等满一个超时。
        QTimer stopPoll;
        stopPoll.setInterval(200);
        QObject::connect(&stopPoll, &QTimer::timeout, &loop, [&] {
            if (shouldStop && shouldStop()) {
                reply->abort();
                loop.quit();
            }
        });
        stopPoll.start();
        loop.exec();
        stopPoll.stop();

        if (!reply->isFinished()) {
            // 超时：打断连接。deleteLater 的对象由下一次 loop.exec() 回收。
            reply->abort();
            reply->deleteLater();
            lastErr = QStringLiteral("timeout");
            interruptibleSleep(int(1500 * (attempt + 1)), shouldStop);
            continue;
        }

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (status == 429) {
            // 限流：退避后重试
            lastErr = QStringLiteral("http_429");
            interruptibleSleep(int(2000 * (attempt + 1)), shouldStop);
            continue;
        }
        if (status < 200 || status >= 300) {
            lastErr = QStringLiteral("http_%1").arg(status);
            interruptibleSleep(int(1500 * (attempt + 1)), shouldStop);
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isNull() || !doc.isObject()) {
            v.ok = false;
            v.error = QStringLiteral("bad_json");
            v.comment = QStringLiteral("响应不是 JSON");
            v.studying = true;
            return v;
        }
        const QJsonObject root = doc.object();
        const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
        lastPromptTokens_ = usage.value(QStringLiteral("prompt_tokens")).toInteger(0);
        lastCompletionTokens_ = usage.value(QStringLiteral("completion_tokens")).toInteger(0);
        totalPromptTokens_ += lastPromptTokens_;
        totalCompletionTokens_ += lastCompletionTokens_;

        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        const QJsonObject message = choices.isEmpty()
                                        ? QJsonObject()
                                        : choices.first().toObject().value(QStringLiteral("message")).toObject();
        const QString content = message.value(QStringLiteral("content")).toString();

        const std::optional<QJsonObject> obj = extractJson(content);
        if (!obj) {
            v.ok = false;
            v.error = QStringLiteral("bad_json");
            v.comment = QStringLiteral("解析失败");
            v.studying = true;
            return v;
        }
        v.studying = obj->value(QStringLiteral("s")).toInt(0) != 0;
        v.comment = obj->value(QStringLiteral("c")).toString().left(16);
        v.ok = true;
        v.promptTokens = lastPromptTokens_;
        v.completionTokens = lastCompletionTokens_;
        return v;
    }

    v.ok = false;
    v.error = lastErr.isEmpty() ? QStringLiteral("unknown") : lastErr;
    v.comment = QStringLiteral("调用失败，暂不打扰");
    v.studying = true;   // 保守处理：不动状态，也不打扰用户
    return v;
}

} // namespace pic2pet
