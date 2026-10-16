#include "TeacherConfig.h"

#include <QSettings>

#include <algorithm>

namespace pic2pet {

bool TeacherApi::isDeepSeek() const {
    // **同时看 base_url 和 model**——只看域名会漏判：代理/自建网关
    // （one-api / new-api 等）转发到 DeepSeek 时域名不是 deepseek.com，
    // 但模型名仍是 deepseek-flash，这时仍必须关思考否则出 bad_json。
    return baseUrl.contains(QStringLiteral("deepseek"), Qt::CaseInsensitive)
           || model.contains(QStringLiteral("deepseek"), Qt::CaseInsensitive);
}

bool TeacherApi::sendThinking() const {
    if (thinking == QLatin1String("disable")) return true;
    if (thinking == QLatin1String("omit")) return false;
    return isDeepSeek();   // auto
}

void TeacherConfig::load(QSettings& s) {
    s.beginGroup(QStringLiteral("teacher"));

    goal = s.value(QStringLiteral("goal"), goal).toString();
    monitorCamera = s.value(QStringLiteral("monitorCamera"), monitorCamera).toBool();
    uploadCamera = s.value(QStringLiteral("uploadCamera"), uploadCamera).toBool();

    s.beginGroup(QStringLiteral("api"));
    api.apiKey = s.value(QStringLiteral("key")).toString();
    api.baseUrl = s.value(QStringLiteral("baseUrl"), api.baseUrl).toString();
    api.model = s.value(QStringLiteral("model"), api.model).toString();
    api.thinking = s.value(QStringLiteral("thinking"), api.thinking).toString();
    api.jsonOutput = s.value(QStringLiteral("jsonOutput"), api.jsonOutput).toBool();
    api.imageDetail = s.value(QStringLiteral("imageDetail"), api.imageDetail).toString();
    api.maxOutputTokens = s.value(QStringLiteral("maxOutputTokens"), api.maxOutputTokens).toInt();
    api.timeoutMs = s.value(QStringLiteral("timeoutMs"), api.timeoutMs).toInt();
    api.maxRetries = s.value(QStringLiteral("maxRetries"), api.maxRetries).toInt();
    api.maxCalls = s.value(QStringLiteral("maxCalls"), api.maxCalls).toInt();
    s.endGroup();

    s.beginGroup(QStringLiteral("capture"));
    capture.composeWidth = s.value(QStringLiteral("composeWidth"), capture.composeWidth).toInt();
    capture.jpegQuality = s.value(QStringLiteral("jpegQuality"), capture.jpegQuality).toInt();
    capture.cameraRatio = s.value(QStringLiteral("cameraRatio"), capture.cameraRatio).toDouble();
    capture.cameraIndex = s.value(QStringLiteral("cameraIndex"), capture.cameraIndex).toInt();
    capture.diffWidth = s.value(QStringLiteral("diffWidth"), capture.diffWidth).toInt();
    capture.diffThreshold = s.value(QStringLiteral("diffThreshold"), capture.diffThreshold).toDouble();
    s.endGroup();

    s.beginGroup(QStringLiteral("rules"));
    rules.idleSoftS = s.value(QStringLiteral("idleSoftS"), rules.idleSoftS).toDouble();
    rules.idleHardS = s.value(QStringLiteral("idleHardS"), rules.idleHardS).toDouble();
    rules.absenceConfirm = s.value(QStringLiteral("absenceConfirm"), rules.absenceConfirm).toInt();
    rules.skinRatioMin = s.value(QStringLiteral("skinRatioMin"), rules.skinRatioMin).toDouble();
    s.endGroup();

    s.beginGroup(QStringLiteral("judge"));
    judge.strikesToWarn = s.value(QStringLiteral("strikesToWarn"), judge.strikesToWarn).toInt();
    judge.gracePeriodS = s.value(QStringLiteral("gracePeriodS"), judge.gracePeriodS).toDouble();
    judge.alertCooldownS = s.value(QStringLiteral("alertCooldownS"), judge.alertCooldownS).toDouble();
    judge.intervalMinS = s.value(QStringLiteral("intervalMinS"), judge.intervalMinS).toDouble();
    judge.intervalMaxS = s.value(QStringLiteral("intervalMaxS"), judge.intervalMaxS).toDouble();
    judge.intervalGrowAfter =
        s.value(QStringLiteral("intervalGrowAfter"), judge.intervalGrowAfter).toInt();
    judge.focusEwmaAlpha = s.value(QStringLiteral("focusEwmaAlpha"), judge.focusEwmaAlpha).toDouble();
    judge.focusHealthyRatio =
        s.value(QStringLiteral("focusHealthyRatio"), judge.focusHealthyRatio).toDouble();
    judge.focusBadRatio = s.value(QStringLiteral("focusBadRatio"), judge.focusBadRatio).toDouble();
    s.endGroup();

    s.beginGroup(QStringLiteral("pomodoro"));
    pomodoro.enabled = s.value(QStringLiteral("enabled"), pomodoro.enabled).toBool();
    pomodoro.focusMinutes = s.value(QStringLiteral("focusMinutes"), pomodoro.focusMinutes).toDouble();
    pomodoro.breakMinutes = s.value(QStringLiteral("breakMinutes"), pomodoro.breakMinutes).toDouble();
    pomodoro.notify = s.value(QStringLiteral("notify"), pomodoro.notify).toBool();
    s.endGroup();

    s.beginGroup(QStringLiteral("webFilter"));
    webFilter.enabled = s.value(QStringLiteral("enabled"), webFilter.enabled).toBool();
    webFilter.entries.clear();
    // 条目用编号键存（跟 Edge 策略自己的注册表布局一致，也避免 URL 里的逗号
    // 把 QStringList 的 INI 逗号分隔搅烂）
    const QStringList keys = s.childKeys();
    QList<int> nums;
    for (const QString& k : keys) {
        bool ok = false;
        const int n = k.toInt(&ok);
        if (ok && n > 0) nums.append(n);
    }
    std::sort(nums.begin(), nums.end());
    for (const int n : nums) {
        const QString v = s.value(QString::number(n)).toString().trimmed();
        if (!v.isEmpty()) webFilter.entries.append(v);
    }
    s.endGroup();

    s.endGroup();
}

void TeacherConfig::save(QSettings& s) const {
    s.beginGroup(QStringLiteral("teacher"));

    s.setValue(QStringLiteral("goal"), goal);
    s.setValue(QStringLiteral("monitorCamera"), monitorCamera);
    s.setValue(QStringLiteral("uploadCamera"), uploadCamera);

    s.beginGroup(QStringLiteral("api"));
    s.setValue(QStringLiteral("key"), api.apiKey);
    s.setValue(QStringLiteral("baseUrl"), api.baseUrl);
    s.setValue(QStringLiteral("model"), api.model);
    s.setValue(QStringLiteral("thinking"), api.thinking);
    s.setValue(QStringLiteral("jsonOutput"), api.jsonOutput);
    s.setValue(QStringLiteral("imageDetail"), api.imageDetail);
    s.setValue(QStringLiteral("maxOutputTokens"), api.maxOutputTokens);
    s.setValue(QStringLiteral("timeoutMs"), api.timeoutMs);
    s.setValue(QStringLiteral("maxRetries"), api.maxRetries);
    s.setValue(QStringLiteral("maxCalls"), api.maxCalls);
    s.endGroup();

    s.beginGroup(QStringLiteral("capture"));
    s.setValue(QStringLiteral("composeWidth"), capture.composeWidth);
    s.setValue(QStringLiteral("jpegQuality"), capture.jpegQuality);
    s.setValue(QStringLiteral("cameraRatio"), capture.cameraRatio);
    s.setValue(QStringLiteral("cameraIndex"), capture.cameraIndex);
    s.setValue(QStringLiteral("diffWidth"), capture.diffWidth);
    s.setValue(QStringLiteral("diffThreshold"), capture.diffThreshold);
    s.endGroup();

    s.beginGroup(QStringLiteral("rules"));
    s.setValue(QStringLiteral("idleSoftS"), rules.idleSoftS);
    s.setValue(QStringLiteral("idleHardS"), rules.idleHardS);
    s.setValue(QStringLiteral("absenceConfirm"), rules.absenceConfirm);
    s.setValue(QStringLiteral("skinRatioMin"), rules.skinRatioMin);
    s.endGroup();

    s.beginGroup(QStringLiteral("judge"));
    s.setValue(QStringLiteral("strikesToWarn"), judge.strikesToWarn);
    s.setValue(QStringLiteral("gracePeriodS"), judge.gracePeriodS);
    s.setValue(QStringLiteral("alertCooldownS"), judge.alertCooldownS);
    s.setValue(QStringLiteral("intervalMinS"), judge.intervalMinS);
    s.setValue(QStringLiteral("intervalMaxS"), judge.intervalMaxS);
    s.setValue(QStringLiteral("intervalGrowAfter"), judge.intervalGrowAfter);
    s.setValue(QStringLiteral("focusEwmaAlpha"), judge.focusEwmaAlpha);
    s.setValue(QStringLiteral("focusHealthyRatio"), judge.focusHealthyRatio);
    s.setValue(QStringLiteral("focusBadRatio"), judge.focusBadRatio);
    s.endGroup();

    s.beginGroup(QStringLiteral("pomodoro"));
    s.setValue(QStringLiteral("enabled"), pomodoro.enabled);
    s.setValue(QStringLiteral("focusMinutes"), pomodoro.focusMinutes);
    s.setValue(QStringLiteral("breakMinutes"), pomodoro.breakMinutes);
    s.setValue(QStringLiteral("notify"), pomodoro.notify);
    s.endGroup();

    s.beginGroup(QStringLiteral("webFilter"));
    s.setValue(QStringLiteral("enabled"), webFilter.enabled);
    int n = 0;
    for (const QString& e : webFilter.entries) {
        const QString v = e.trimmed();
        if (!v.isEmpty()) s.setValue(QString::number(++n), v);
    }
    s.endGroup();

    s.endGroup();
}

} // namespace pic2pet
