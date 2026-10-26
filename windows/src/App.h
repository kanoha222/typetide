//
//  App.h — 主编排（对应 macOS 的 TriggerController + AppDelegate）：
//  隐藏主窗口接收热键 / 托盘 / 工作线程消息，串起取词 → 翻译 → 弹窗/替换。
//  每次触发在主线程冻结该入口绑定的 profile（execution::ContextPtr），取词、重试、
//  改目标语言都沿用这份快照；之后保存的设置只影响下一次触发。
//
#pragma once
#include <windows.h>
#include "Language.h"
#include "SelectionCapture.h"
#include "TranslationExecution.h"
#include "TranslationTypes.h"
#include "TrayIcon.h"
#include <cstdint>
#include <optional>
#include <string>

// 主窗口自定义消息
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_TR_DELTA = WM_APP + 2;    // wParam=reqId, lParam=std::wstring*（接收方释放）
constexpr UINT WM_APP_TR_DONE = WM_APP + 3;     // wParam=reqId, lParam=DoneMsg*
constexpr UINT WM_APP_REWRITE_DONE = WM_APP + 4;// lParam=RewriteResult*
constexpr UINT WM_APP_UPDATE = WM_APP + 5;      // 有新版本
constexpr UINT WM_APP_OPEN_SETTINGS = WM_APP + 6;
constexpr UINT WM_APP_MODELS = WM_APP + 7;          // lParam=std::vector<std::wstring>*（接收方释放）
constexpr UINT WM_APP_READ_CAPTURED = WM_APP + 8;   // lParam=CapturedSelection*（接收方释放）
constexpr UINT WM_APP_REWRITE_CAPTURED = WM_APP + 9;// 同上

constexpr wchar_t kAppWindowClass[] = L"TypeTideApp";

class App {
public:
    static App& shared();

    bool init(HINSTANCE inst);
    HWND hwnd() const { return hwnd_; }
    bool shortcutsRegistered() const { return shortcutsReady_; }

    /// 根据开关 + 快捷键 + 划词设置重新装配触发（设置变化后调用）
    void applyEnabledState();
    void openSettings();

private:
    App() = default;
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    struct DoneMsg {
        bool ok;
        std::wstring error;
    };
    struct RewriteResult {
        bool ok;
        std::wstring text;    // 交付文本（ok 时）：custom 原样，preset 去首尾空白
        std::wstring error;
        bool isWholeField;
        RECT anchor;
        bool hasAnchor;
        std::wstring original;
        Language target;
        uint64_t generation;  // 过期（已有更新的用户翻译）则丢弃，不再粘贴
    };
    struct CapturedSelection {
        capture::Capture cap;
        execution::ContextPtr context;  // 触发时冻结的 profile
    };

    void performEntry(ProfileEntry entry, ShortcutAction action);
    void handleRead(execution::ContextPtr context);
    void handleRewrite(execution::ContextPtr context);
    void captureFallbackAsync(UINT message, execution::ContextPtr context);
    void presentRead(const capture::Capture& cap, execution::ContextPtr context);
    void proceedRewrite(const capture::Capture& cap, execution::ContextPtr context);
    void openPopupSession(const capture::Capture& cap, execution::ContextPtr context, bool replaceSelectAll);
    void replaceDirectly(const capture::Capture& cap, execution::ContextPtr context);
    /// 新的用户翻译使上一次失效：取消其流，丢弃其尚未粘贴的直接替换结果。
    void beginUserTranslation();
    /// 超长输入直接弹错误提示（LLM 又慢又贵，弹窗也放不下）；返回 true = 已拦截
    bool rejectIfTooLong(const capture::Capture& cap, const execution::ContextPtr& context);
    void startStream(std::optional<Language> targetOverride);
    void setupSelectionTrigger();
    bool foregroundAppSkipped() const;
    void showErrorPopup(const std::wstring& original, Language target, RECT anchor, const std::wstring& error);
    void quit();

    HWND hwnd_ = nullptr;
    TrayIcon tray_;
    uint64_t currentReq_ = 0;                  // 当前弹窗会话的流
    uint64_t rewriteReq_ = 0;                  // 当前直接替换的流（弹窗关闭不会取消它）
    uint64_t generation_ = 0;
    execution::ContextPtr sessionContext_;     // 弹窗会话的冻结 profile（重试/改目标语言沿用）
    std::wstring sessionText_;
    bool currentReplaceSelectAll_ = false;     // 弹窗 Replace 时是否先全选
    bool hotkeyWarningShown_ = false;
    bool readShortcutRegistered_ = false;
    bool rewriteShortcutRegistered_ = false;
    bool shortcutsReady_ = false;
    UINT taskbarCreatedMsg_ = 0;   // explorer 重启后重挂托盘图标
};
