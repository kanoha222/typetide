//
//  Settings.h — 全局设置（对应 macOS 的 AppSettings），
//  JSON 持久化到 %APPDATA%\TypeTide\settings.json。
//
#pragma once
#include <windows.h>
#include "Language.h"
#include "TranslationProfile.h"
#include "TranslationTypes.h"
#include <cstdint>
#include <string>
#include <vector>

/// 划词触发方式
enum class SelectionTrigger {
    None,  // 仅快捷键
    Icon,  // 划词后显示小图标，点击翻译
    Auto,  // 划词后直接弹出翻译
};

enum class StorageState {
    Ready,
    Invalid,
    UnsupportedSchema,
    IoError,
};

/// 全局快捷键（RegisterHotKey 语义）
struct Hotkey {
    UINT modifiers = MOD_ALT;  // MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN 组合
    UINT vk = 'D';

    std::wstring displayString() const;
    bool isConfigured() const { return vk != 0; }
    bool operator==(const Hotkey&) const = default;
};

struct SettingsSnapshot {
    ProfileConfiguration profileConfig;
    uint64_t connectionEpoch = 0;
    TranslationBackend backend = TranslationBackend::BuiltIn;
    std::string ollamaHost;
    int ollamaPort = 11434;
    std::string ollamaModel;
    std::string openAIBaseURL;
    std::string openAIModel;
    std::string openAIKey; // Resolved secret: memory only, never serialized/logged.
    std::string openAICredentialRef;
    bool hasCompletedFirstLaunch = false;
};

struct Settings {
    bool enabled = true;
    TranslationBackend backend = TranslationBackend::BuiltIn;

    // Ollama
    std::string ollamaHost = "http://127.0.0.1";
    int ollamaPort = 11434;
    std::string ollamaModel = "qwen2.5:3b";

    // OpenAI 兼容
    std::string openAIBaseURL = "https://api.openai.com/v1";
    std::string openAIKey;
    std::string openAIModel = "gpt-5-mini";

    // 语言（native = 母语，foreign = 外语）
    Language nativeLanguage = Language::Chinese;
    Language foreignLanguage = Language::English;
    TranslationDirection readDirection = TranslationDirection::Auto;
    TranslationDirection rewriteDirection = TranslationDirection::Auto;

    // 快捷键
    Hotkey readShortcut{MOD_ALT, 'D'};
    Hotkey rewriteShortcut{MOD_ALT, 'R'};
    ShortcutAction readShortcutAction = ShortcutAction::SmartPopup;
    ShortcutAction rewriteShortcutAction = ShortcutAction::SmartReplace;

    // 行为
    SelectionTrigger selectionTrigger = SelectionTrigger::None;
    bool rewritePreview = false;  // false = 直接替换
    RewriteStyle readStyle = RewriteStyle::Faithful;
    RewriteStyle rewriteStyle = RewriteStyle::Faithful;

    // 跳过的应用（exe 名，不区分大小写，可不带 .exe）
    std::vector<std::wstring> skipApps;

    bool hasCompletedFirstLaunch = false;
    long long lastUpdateCheck = 0;  // Unix 秒

    // 翻译配置集合（v2 核心）
    ProfileConfiguration profileConfig = profiles::Defaults();

    // --- 存储与事务状态 ---
    StorageState storageState() const { return storageState_; }
    const std::string& storageError() const { return storageError_; }
    bool isReadOnly() const { return storageState_ != StorageState::Ready; }
    uint64_t connectionEpoch() const { return connectionEpoch_; }
    const std::string& credentialRef() const { return credentialRef_; }

    // --- 事务提交与快照 ---
    bool commit(const Settings& candidate, std::string* error = nullptr);
    SettingsSnapshot snapshot() const;

    // --- 持久化 ---
    static Settings& shared();
    void load();
    /// 原子保存；失败时保留旧文件并返回 false。
    bool save() const;
    static std::wstring filePath();

private:
    StorageState storageState_ = StorageState::Ready;
    std::string storageError_;
    uint64_t connectionEpoch_ = 0;
    std::string credentialRef_;
};
