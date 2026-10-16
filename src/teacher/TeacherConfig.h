#pragma once

#include <QString>
#include <QStringList>

class QSettings;

namespace pic2pet {

/**
 * 模型服务（L1 视觉判定用的 OpenAI 兼容端点）。
 *
 * 默认 DeepSeek：`deepseek-flash` 原生支持图像输入，且**必须关思考**——
 * 思考链 token 计入 completion_tokens，实测开思考要 613 tokens 才憋出正文，
 * 关掉只要 14。其他 OpenAI 兼容服务（Moonshot / 通义 / Ollama…）不认识
 * `thinking` 字段，硬发会 400，所以按域名+模型名自动判断发不发。
 */
struct TeacherApi {
    QString apiKey;              // 只存本地 INI，**不写日志、不进报告**
    QString baseUrl = QStringLiteral("https://api.deepseek.com");
    QString model = QStringLiteral("deepseek-flash");
    // thinking: "auto" = 按域名/模型名自动判断；"disable" = 总是发；"omit" = 从不发
    QString thinking = QStringLiteral("auto");
    bool jsonOutput = true;      // response_format: json_object（OpenAI 标准字段）
    QString imageDetail = QStringLiteral("low");   // 服务端先缩到 512x512，约 159 tokens/张
    int maxOutputTokens = 64;    // 关思考时 14 tokens 就够；别调小，思考会吃光额度
    int timeoutMs = 30000;
    int maxRetries = 2;
    /** 单次会话的调用预算硬护栏。超过后不再发请求（防调参失控） */
    int maxCalls = 600;

    /** 是否 DeepSeek 系（同时看 base_url 和 model：自建网关转发时域名会变） */
    bool isDeepSeek() const;
    /** 是否需要发送 thinking 参数 */
    bool sendThinking() const;
};

/** 图像采集与压缩（合成图只发一次请求、只算一份图片 token） */
struct TeacherCaptureCfg {
    int composeWidth = 640;      // 合成图宽度；detail=low 时服务端会再缩到 512
    int jpegQuality = 60;        // 30~95
    double cameraRatio = 0.36;   // 摄像头占宽度比例（左右拼接；0.36 能看清低头姿态）
    int cameraIndex = 0;
    int diffWidth = 160;         // 帧差检测的缩略图宽度
    double diffThreshold = 6.0;  // 帧差均值超过此值认为画面有变化
};

/** L0 本地规则阈值 */
struct TeacherRuleCfg {
    double idleSoftS = 180.0;    // 空闲超过此值开始降低「学习中」权重（预留，暂未用）
    double idleHardS = 600.0;    // 空闲超过此值 + 画面静止 → 硬判分心（预留，暂未用）
    int absenceConfirm = 3;      // 连续多少帧确认无人才判离开（单帧 Haar/肤色会抖）
    double skinRatioMin = 0.12;  // 肤色占比阈值：实测人在 ~40%、空背景 ~5%
};

/** L1/L2 判定与状态机参数 */
struct TeacherJudgeCfg {
    int strikesToWarn = 2;       // 模型判分心需连续几次才告警（L0 硬规则立即告警）
    double gracePeriodS = 180.0; // 启动宽限期
    double alertCooldownS = 300.0;
    double intervalMinS = 60.0;  // 分心时拉满采样
    double intervalMaxS = 300.0; // 持续在学拉到这么疏（省调用）
    int intervalGrowAfter = 2;   // 连续几次学习后拉长间隔
    double focusEwmaAlpha = 0.4;
    double focusHealthyRatio = 0.7;   // EWMA 高于此值 → 学习中
    double focusBadRatio = 0.4;       // EWMA 低于此值 → 分心
};

/** 课堂模式（番茄钟）：学一段、休息一段；休息期间完全不抓拍 */
struct TeacherPomodoroCfg {
    bool enabled = false;
    double focusMinutes = 40.0;  // 按一节课的注意力节奏设的
    double breakMinutes = 10.0;  // 0 = 跳过后直接进入下一轮专注
    bool notify = true;
};

/**
 * 网站白名单（Edge 上网限制）：开启后 Edge 只能访问 entries 里的网站。
 *
 * 语义**直通 Edge 原生策略**（URLBlocklist 写 `*` 全拦 + URLAllowlist 写例外）：
 *   - 条目格式 `[scheme://][.]host[:port][/path]`，路径为**前缀匹配**
 *     （`hosting.com/good_path` 匹配所有 `/good_path…` 开头的路径）
 *   - 主机名不带点前缀 = 含全部子域（`bilibili.com` 匹配 `www.bilibili.com`）；
 *     带点前缀（`.www.bilibili.com`）= 只匹配这一个精确主机名
 *   - 不带 scheme = http/https 都放行；带 `https://` 则只放行 https
 *   - 只影响 Microsoft Edge；本地 file:// 链接会被一并拦掉（Edge 策略的已知行为）
 *
 * 生命周期：**独立于 AI 教官运行状态**。
 *   - enabled = "用户想要这个限制"，**不等于**策略此刻真的挂着
 *     （策略在 HKLM，见 WebFilter.h；正常退出会清掉、强杀会留着）
 *   - Pic2Pet 退出时自动清除（恢复 Edge），会弹一次 UAC
 *   - 异常被杀死：策略留着继续生效，下次启动 UI 会如实显示实际状态；
 *     **不做自动重放** —— 开机就弹管理员授权没人受得了
 */
struct WebFilterCfg {
    bool enabled = false;
    QStringList entries;      // 白名单条目（Edge 策略格式，见上面语义）
};

/**
 * AI 教官的全部配置。**默认配置 = 目标「学习」+ DeepSeek + 60~300s 自适应间隔 +
 * 课堂模式关**，右键「AI 教官」一键启动用的就是它；详细项在设置页第 7 页改。
 */
struct TeacherConfig {
    QString goal = QStringLiteral("学习");
    bool monitorCamera = true;   // 是否开摄像头（本地做人脸/肤色在场检测）
    bool uploadCamera = true;    // 摄像头画面是否随截图一起上传给模型

    TeacherApi api;
    TeacherCaptureCfg capture;
    TeacherRuleCfg rules;
    TeacherJudgeCfg judge;
    TeacherPomodoroCfg pomodoro;
    WebFilterCfg webFilter;

    /** 从 INI 的 [teacher] 组读（缺项用默认值） */
    void load(QSettings& s);
    /** 写回 INI 的 [teacher] 组 */
    void save(QSettings& s) const;
};

} // namespace pic2pet
