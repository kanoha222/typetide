//
//  App.h — 主编排（对应 macOS 的 TriggerController + AppDelegate）：
//  隐藏主窗口接收热键 / 托盘 / 工作线程消息，串起取词 → 翻译 → 弹窗/替换。
//  每次触发在主线程冻结该入口绑定的 profile（execution::ContextPtr），取词、重试、
//  改目标语言都沿用这份快照；之后保存的设置只影响下一次触发。
//  会话由 session::Ledger 记账：回调带着创建时的 Ticket，弹窗关闭只影响它自己的会话，
//  替换在最后一次按键前再核对会话与目标窗口。
//
#pragma once
#include <windows.h>
#include "Language.h"
#include "OllamaModels.h"
#include "SelectionCapture.h"
#include "TranslationExecution.h"
#include "TranslationSession.h"
#include "TranslationTypes.h"
#include "TrayIcon.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// 主窗口自定义消息
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_TR_DELTA = WM_APP + 2;    // lParam=DeltaMsg*（接收方释放）
constexpr UINT WM_APP_TR_DONE = WM_APP + 3;     // lParam=DoneMsg*
constexpr UINT WM_APP_REWRITE_DONE = WM_APP + 4;// lParam=RewriteResult*
constexpr UINT WM_APP_UPDATE = WM_APP + 5;      // 有新版本
constexpr UINT WM_APP_OPEN_SETTINGS = WM_APP + 6;
constexpr UINT WM_APP_MODELS = WM_APP + 7;          // lParam=ollamamodels::Discovery*（接收方释放）
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
    void openProfiles();

private:
    App() = default;
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    struct DeltaMsg {
        session::Ticket ticket;
        std::wstring delta;
    };
    struct DoneMsg {
        session::Ticket ticket;
        bool ok;
        std::wstring error;
    };
    struct RewriteResult {
        session::Ticket ticket;  // 过期（已有更新的用户翻译）则丢弃，不再粘贴
        bool ok;
        std::wstring text;       // 交付文本（ok 时）：custom 原样，preset 去首尾空白
        std::wstring error;
        bool isWholeField;
        RECT anchor;
        HWND window;             // 取词时的前台窗口（粘贴目标）
        std::wstring original;
        Language target;
    };
    /// 一次触发在主线程冻结的全部输入；异步取词带着它回来。
    struct Trigger {
        execution::ContextPtr context;  // 已提交快照里该入口绑定的 profile
        uint64_t seq = 0;               // 触发序号：取词晚到且已有更新的触发 → 丢弃
        bool preview = false;           // 触发时的改写预览开关
    };
    struct CapturedSelection {
        capture::Capture cap;
        Trigger trigger;
    };

    Trigger beginTrigger(ProfileEntry entry, ShortcutAction action);
    void performEntry(ProfileEntry entry, ShortcutAction action);
    void handleRead(Trigger trigger);
    void handleRewrite(Trigger trigger);
    void captureFallbackAsync(UINT message, Trigger trigger);
    void presentRead(const capture::Capture& cap, execution::ContextPtr context);
    void proceedRewrite(const capture::Capture& cap, Trigger trigger);
    void openPopupSession(const capture::Capture& cap, execution::ContextPtr context, bool replaceSelectAll);
    void installPopupHandlers(uint64_t sessionId);
    void replaceDirectly(const capture::Capture& cap, execution::ContextPtr context);
    /// 新的用户翻译：之前的会话全部失效（取消其流，未提交的粘贴不再落下）。返回新会话。
    uint64_t beginUserTranslation();
    void cancelRequests(const std::vector<uint64_t>& requestIds);
    void onPopupReplace(uint64_t sessionId);
    void onDelta(const DeltaMsg& msg);
    void onTranslationDone(const DoneMsg& done);
    void onRewriteDone(RewriteResult& result);
    void onCaptured(UINT message, CapturedSelection& captured);
    void onModelsListed(const ollamamodels::Discovery& discovery);
    void onTrayEvent(UINT event);
    void onHotkey(WPARAM id);
    void bindProfile(ProfileEntry entry, const std::string& profileId);
    /// 超长输入直接弹错误提示（LLM 又慢又贵，弹窗也放不下）；返回 true = 已拦截
    bool rejectIfTooLong(const capture::Capture& cap, const execution::ContextPtr& context);
    void startStream(std::optional<Language> targetOverride);
    void setupSelectionTrigger();
    bool foregroundAppSkipped() const;
    void showErrorPopup(const std::wstring& original, Language target, RECT anchor, const std::wstring& error);
    void quit();
    void finishQuit();

    HWND hwnd_ = nullptr;
    TrayIcon tray_;
    session::Ledger ledger_;
    uint64_t triggerSeq_ = 0;                  // 触发序号（异步取词按它判过期）
    uint64_t popupSession_ = 0;                // 当前弹窗所属的会话
    execution::ContextPtr sessionContext_;     // 弹窗会话的冻结 profile（重试/改目标语言沿用）
    std::wstring sessionText_;
    HWND sessionWindow_ = nullptr;             // 弹窗会话取词时的前台窗口（Replace 的粘贴目标）
    bool currentReplaceSelectAll_ = false;     // 弹窗 Replace 时是否先全选
    bool hotkeyWarningShown_ = false;
    bool readShortcutRegistered_ = false;
    bool rewriteShortcutRegistered_ = false;
    bool shortcutsReady_ = false;
    UINT taskbarCreatedMsg_ = 0;   // explorer 重启后重挂托盘图标
    int quitAttempts_ = 0;         // 退出时等剪贴板事务的轮询次数
};
