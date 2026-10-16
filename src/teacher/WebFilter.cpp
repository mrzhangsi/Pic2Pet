#include "WebFilter.h"

#ifdef Q_OS_WIN

#include <QStringList>

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

const wchar_t* kPolicyRoot = L"SOFTWARE\\Policies\\Microsoft\\Edge";
const wchar_t* kBackupRoot = L"SOFTWARE\\Pic2Pet\\WebFilterBackup";
const wchar_t* kBlocklistName = L"URLBlocklist";
const wchar_t* kAllowlistName = L"URLAllowlist";
const wchar_t* kOwnedMarker = L"owned";   // 备份键里有它 = 当前策略值是我们写的

/** 注册表值的原始快照（按原名、原类型、原数据整体搬运） */
struct RegValue {
    std::wstring name;
    DWORD type = REG_NONE;
    std::vector<BYTE> data;
};

std::wstring widen(const QString& s) { return std::wstring(s.toStdWString()); }

bool isAllDigits(const std::wstring& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

std::wstring joinPath(const wchar_t* a, const wchar_t* b) {
    return std::wstring(a) + L"\\" + b;
}

bool openKey(HKEY root, const wchar_t* path, REGSAM access, HKEY* out) {
    return RegOpenKeyExW(root, path, 0, access, out) == ERROR_SUCCESS;
}

bool createKey(HKEY root, const wchar_t* path, HKEY* out) {
    DWORD disp = 0;
    return RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, out, &disp)
           == ERROR_SUCCESS;
}

/**
 * 枚举键下所有值。两遍式查询：先问尺寸再取数据，
 * 所以 >8KB 的大值也能完整拿到。名字超长（>512 字符）的值跳过。
 */
std::vector<RegValue> enumValues(HKEY key) {
    std::vector<RegValue> out;
    for (DWORD i = 0;; ++i) {
        wchar_t name[512];
        DWORD nameLen = 512;
        DWORD type = 0;
        DWORD dataLen = 0;
        LONG r = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, nullptr, &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;   // 名字太长等：跳过这个索引

        std::vector<BYTE> data(dataLen);
        nameLen = 512;
        r = RegEnumValueW(key, i, name, &nameLen, nullptr, &type,
                          data.empty() ? nullptr : data.data(), &dataLen);
        if (r != ERROR_SUCCESS) continue;

        RegValue v;
        v.name = name;
        v.type = type;
        v.data = std::move(data);
        out.push_back(std::move(v));
    }
    return out;
}

DWORD countSubKeys(HKEY key) {
    DWORD n = 0;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &n, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
    return n;
}

bool setStringValue(HKEY key, const std::wstring& name, const std::wstring& value) {
    const DWORD bytes = DWORD((value.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(value.c_str()), bytes)
           == ERROR_SUCCESS;
}

bool setRawValue(HKEY key, const RegValue& v) {
    return RegSetValueExW(key, v.name.c_str(), 0, v.type,
                          v.data.empty() ? nullptr : v.data.data(), DWORD(v.data.size()))
           == ERROR_SUCCESS;
}

void deleteValue(HKEY key, const std::wstring& name) {
    RegDeleteValueW(key, name.c_str());
}

/** 删掉键下所有编号值（Edge 列表策略的条目形式） */
void deleteNumberedValues(HKEY key) {
    for (const RegValue& v : enumValues(key)) {
        if (isAllDigits(v.name)) deleteValue(key, v.name);
    }
}

/** 键空了才删键本体（不碰用户/组织预先存在的东西） */
void deleteKeyIfEmpty(HKEY root, const wchar_t* path) {
    HKEY key = nullptr;
    if (!openKey(root, path, KEY_READ, &key)) return;
    const bool empty = enumValues(key).empty() && countSubKeys(key) == 0;
    RegCloseKey(key);
    if (empty) RegDeleteKeyExW(root, path, 0, 0);
}

/**
 * 把策略键里的现存值整体备份到我们自己的命名空间。
 * 返回 false = 有无法原样写回的类型（REG_BINARY 等），调用方应放弃开启。
 */
bool backupValues(HKEY backupParent, const wchar_t* policySubName) {
    HKEY src = nullptr;
    if (!openKey(HKEY_CURRENT_USER, joinPath(kPolicyRoot, policySubName).c_str(),
                 KEY_READ, &src)) {
        return true;   // 没有预存值，无需备份
    }
    const std::vector<RegValue> values = enumValues(src);
    RegCloseKey(src);
    if (values.empty()) return true;

    for (const RegValue& v : values) {
        if (v.type != REG_SZ && v.type != REG_MULTI_SZ && v.type != REG_EXPAND_SZ
            && v.type != REG_DWORD) {
            return false;
        }
    }

    HKEY dst = nullptr;
    if (!createKey(backupParent, policySubName, &dst)) return false;
    bool ok = true;
    for (const RegValue& v : values) {
        if (!setRawValue(dst, v)) ok = false;
    }
    RegCloseKey(dst);
    return ok;
}

/** 从备份键把原值写回策略键，然后删掉这个备份子键 */
void restoreValues(HKEY backupParent, const wchar_t* policySubName) {
    HKEY src = nullptr;
    if (!openKey(backupParent, policySubName, KEY_READ, &src)) return;
    const std::vector<RegValue> values = enumValues(src);
    RegCloseKey(src);
    if (values.empty()) return;

    HKEY dst = nullptr;
    if (!createKey(HKEY_CURRENT_USER, joinPath(kPolicyRoot, policySubName).c_str(), &dst)) {
        return;
    }
    for (const RegValue& v : values) setRawValue(dst, v);
    RegCloseKey(dst);

    deleteKeyIfEmpty(HKEY_CURRENT_USER, joinPath(kBackupRoot, policySubName).c_str());
}

bool backupOwnedByUs() {
    HKEY key = nullptr;
    if (!openKey(HKEY_CURRENT_USER, kBackupRoot, KEY_READ, &key)) return false;
    DWORD data = 0;
    DWORD size = sizeof(data);
    const bool owned = RegQueryValueExW(key, kOwnedMarker, nullptr, nullptr,
                                        reinterpret_cast<BYTE*>(&data), &size)
                       == ERROR_SUCCESS;
    RegCloseKey(key);
    return owned;
}

bool markBackupOwned() {
    HKEY key = nullptr;
    if (!createKey(HKEY_CURRENT_USER, kBackupRoot, &key)) return false;
    DWORD one = 1;
    const bool ok = RegSetValueExW(key, kOwnedMarker, 0, REG_DWORD,
                                   reinterpret_cast<const BYTE*>(&one), sizeof(one))
                    == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

void removeBackupMarker() {
    HKEY key = nullptr;
    if (openKey(HKEY_CURRENT_USER, kBackupRoot, KEY_ALL_ACCESS, &key)) {
        RegDeleteValueW(key, kOwnedMarker);
        RegCloseKey(key);
    }
    deleteKeyIfEmpty(HKEY_CURRENT_USER, kBackupRoot);
}

} // namespace

namespace pic2pet {

bool WebFilter::isSupported() { return true; }

bool WebFilter::hasMachinePolicyConflict() {
    const std::wstring paths[] = {joinPath(kPolicyRoot, kBlocklistName),
                                  joinPath(kPolicyRoot, kAllowlistName)};
    for (const std::wstring& p : paths) {
        HKEY key = nullptr;
        if (openKey(HKEY_LOCAL_MACHINE, p.c_str(), KEY_READ, &key)) {
            const bool hasValues = !enumValues(key).empty();
            RegCloseKey(key);
            if (hasValues) return true;
        }
    }
    return false;
}

bool WebFilter::isActive() {
    HKEY key = nullptr;
    if (!openKey(HKEY_CURRENT_USER, joinPath(kPolicyRoot, kBlocklistName).c_str(),
                 KEY_READ, &key)) {
        return false;
    }
    wchar_t data[8] = {};
    DWORD size = sizeof(data);
    DWORD type = 0;
    const LONG r = RegQueryValueExW(key, L"1", nullptr, &type,
                                    reinterpret_cast<BYTE*>(data), &size);
    RegCloseKey(key);
    // 签名：编号值 1 == "*"（全拦）。只有我们会这么写
    return r == ERROR_SUCCESS && type == REG_SZ && std::wstring(data) == L"*";
}

bool WebFilter::apply(const QStringList& rawEntries) {
    error_.clear();

    // ---- 1. 校验条目 ----
    QStringList entries;
    for (const QString& e : rawEntries) {
        const QString v = e.trimmed();
        if (v.isEmpty()) continue;
        bool hasSpace = false;
        for (const QChar& c : v) {
            if (c.isSpace()) hasSpace = true;
        }
        if (hasSpace) {
            error_ = QStringLiteral("条目不能含空格：%1").arg(v);
            return false;
        }
        entries.append(v);
    }
    if (entries.isEmpty()) {
        error_ = QStringLiteral("白名单为空：先添加至少一个网站再开启");
        return false;
    }
    if (entries.size() > 1000) {
        error_ = QStringLiteral("Edge 策略上限 1000 条，当前 %1 条").arg(entries.size());
        return false;
    }

    // ---- 2. 组织策略冲突：HKLM 同名策略会覆盖用户级写入，且不是我们的东西 ----
    if (hasMachinePolicyConflict()) {
        error_ = QStringLiteral(
            "检测到组织/学校下发的 Edge 策略（HKLM 已有 URLBlocklist/URLAllowlist），"
            "用户级白名单会被它覆盖。请先移除组织策略再开启。");
        return false;
    }

    // ---- 3. 幂等：当前挂着的就是我们想写的状态 → 什么都不做 ----
    {
        HKEY key = nullptr;
        bool same = false;
        if (openKey(HKEY_CURRENT_USER, joinPath(kPolicyRoot, kAllowlistName).c_str(),
                    KEY_READ, &key)) {
            std::vector<std::wstring> current;
            for (const RegValue& v : enumValues(key)) {
                if (isAllDigits(v.name) && v.type == REG_SZ && !v.data.empty()) {
                    current.emplace_back(reinterpret_cast<const wchar_t*>(v.data.data()));
                }
            }
            std::vector<std::wstring> want;
            for (const QString& e : entries) want.push_back(widen(e));
            std::sort(current.begin(), current.end());
            std::sort(want.begin(), want.end());
            same = (current == want);
        }
        if (key) RegCloseKey(key);
        if (same && isActive()) return true;
    }

    // ---- 4. 备份预存值（我们上次写的除外：owned 标记在备份键里） ----
    if (!backupOwnedByUs()) {
        HKEY backupParent = nullptr;
        if (!createKey(HKEY_CURRENT_USER, kBackupRoot, &backupParent)) {
            error_ = QStringLiteral("无法创建备份注册表键（HKCU\\SOFTWARE\\Pic2Pet）");
            return false;
        }
        const bool okBlock = backupValues(backupParent, kBlocklistName);
        const bool okAllow = backupValues(backupParent, kAllowlistName);
        RegCloseKey(backupParent);
        if (!okBlock || !okAllow) {
            error_ = QStringLiteral(
                "Edge 策略里已有无法识别的预存值，为避免弄丢它们，已拒绝开启。"
                "可手动清理 HKCU\\SOFTWARE\\Policies\\Microsoft\\Edge 下的 "
                "URLBlocklist / URLAllowlist 后重试。");
            return false;
        }
        // 即使两个键都没有预存值，也打下 owned 标记（声明所有权）。
        // 打不上标记 = 无法证明所有权，宁可拒绝开启（否则以后想清都清不干净）
        if (!markBackupOwned()) {
            error_ = QStringLiteral("无法标记备份所有权（HKCU\\SOFTWARE\\Pic2Pet）");
            return false;
        }
    }

    // ---- 5. 写策略 ----
    HKEY blockKey = nullptr;
    if (!createKey(HKEY_CURRENT_USER, joinPath(kPolicyRoot, kBlocklistName).c_str(),
                   &blockKey)) {
        error_ = QStringLiteral("写入 URLBlocklist 失败（注册表访问被拒？）");
        return false;
    }
    for (const RegValue& v : enumValues(blockKey)) deleteValue(blockKey, v.name);
    const bool blockOk = setStringValue(blockKey, L"1", L"*");
    RegCloseKey(blockKey);

    HKEY allowKey = nullptr;
    if (!createKey(HKEY_CURRENT_USER, joinPath(kPolicyRoot, kAllowlistName).c_str(),
                   &allowKey)) {
        error_ = QStringLiteral("写入 URLAllowlist 失败（注册表访问被拒？）");
        return false;
    }
    for (const RegValue& v : enumValues(allowKey)) deleteValue(allowKey, v.name);
    bool allowOk = true;
    for (int i = 0; i < entries.size(); ++i) {
        if (!setStringValue(allowKey, std::to_wstring(i + 1), widen(entries[i]))) {
            allowOk = false;
        }
    }
    RegCloseKey(allowKey);

    if (!blockOk || !allowOk) {
        error_ = QStringLiteral("写入策略值失败");
        return false;
    }
    return true;
}

void WebFilter::clear() {
    error_.clear();

    // 没有所有权标记 = 我们从来没写过策略。这时**什么都不碰**：
    // 键里的值可能是用户/组织自己配的，删了就是弄丢别人的东西。
    // （正常关闭、异常被杀后的自愈重放都带标记；标记只可能被手动删除）
    if (!backupOwnedByUs()) return;

    // 1. 删掉我们写的编号值（URLBlocklist 的 "*" 与 URLAllowlist 的条目）
    HKEY key = nullptr;
    if (openKey(HKEY_CURRENT_USER, joinPath(kPolicyRoot, kBlocklistName).c_str(),
                KEY_ALL_ACCESS, &key)) {
        deleteNumberedValues(key);
        RegCloseKey(key);
    }
    if (openKey(HKEY_CURRENT_USER, joinPath(kPolicyRoot, kAllowlistName).c_str(),
                KEY_ALL_ACCESS, &key)) {
        deleteNumberedValues(key);
        RegCloseKey(key);
    }

    // 2. 有备份 → 原样写回用户/组织预存值
    {
        HKEY bp = nullptr;
        if (openKey(HKEY_CURRENT_USER, kBackupRoot, KEY_READ, &bp)) {
            restoreValues(bp, kBlocklistName);
            restoreValues(bp, kAllowlistName);
            RegCloseKey(bp);
        }
        removeBackupMarker();
    }

    // 3. 清掉因此变空的键（只删空的，绝不碰还有值的）
    deleteKeyIfEmpty(HKEY_CURRENT_USER, joinPath(kPolicyRoot, kBlocklistName).c_str());
    deleteKeyIfEmpty(HKEY_CURRENT_USER, joinPath(kPolicyRoot, kAllowlistName).c_str());
    deleteKeyIfEmpty(HKEY_CURRENT_USER, kPolicyRoot);
}

} // namespace pic2pet

#else // !Q_OS_WIN

namespace pic2pet {

bool WebFilter::isSupported() { return false; }

bool WebFilter::hasMachinePolicyConflict() { return false; }

bool WebFilter::isActive() { return false; }

bool WebFilter::apply(const QStringList&) {
    error_ = QStringLiteral("网站白名单只支持 Windows");
    return false;
}

void WebFilter::clear() {}

} // namespace pic2pet

#endif // Q_OS_WIN
