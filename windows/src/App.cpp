#include "App.h"
#include "Clipboard.h"
#include "Hooks.h"
#include "LocalDiagnostics.h"
#include "OllamaModels.h"
#include "PopupWindow.h"
#include "ProfileBinding.h"
#include "SelectionIcon.h"
#include "SelectionMonitor.h"
#include "Settings.h"
#include "SettingsWindow.h"
#include "TextReplacer.h"
#include "Translator.h"
#include "UpdateChecker.h"
#include "Util.h"
#include <shellapi.h>
#include <psapi.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace {
constexpr int kReadHotkeyId = 1;
constexpr int kRewriteHotkeyId = 2;
// 超过这个长度不翻译：LLM 又慢又贵，弹窗也展示不下
constexpr size_t kMaxInputChars = 5000;
constexpr wchar_t kNoProfileError[] = L"No translation profile is bound to this action.";
// 退出时给仍在还原剪贴板的替换/取词事务的最长等待：kQuitRetries × kQuitRetryMs
constexpr UINT_PTR kQuitTimerId = 1;
constexpr UINT kQuitRetryMs = 100;
constexpr int kQuitRetries = 30;
} // namespace

App& App::shared() {
    static App app;
    return app;
}

LRESULT CALLBACK App::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) return 0;
    App& self = App::shared();
    if (self.hwnd_ == nullptr || hwnd == self.hwnd_) {
        self.hwnd_ = hwnd;
        return self.handle(msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool App::init(HINSTANCE inst) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kAppWindowClass;
    RegisterClassW(&wc);

    // 隐藏主窗口（顶层不可见；托盘与热键的宿主）
    hwnd_ = CreateWindowExW(0, kAppWindowClass, L"TypeTide", 0, 0, 0, 0, 0,
                            nullptr, nullptr, inst, nullptr);
    if (!hwnd_) return false;

    tray_.onToggleEnabled = [this] {
        Settings& s = Settings::shared();
        s.enabled = !s.enabled;
        s.save();
        applyEnabledState();
    };
    tray_.onOpenSettings = [this] { openSettings(); };
    tray_.onOpenReleases = [] { updatechecker::OpenReleasesPage(); };
    tray_.onQuit = [this] { quit(); };
    tray_.onBindProfile = [this](ProfileEntry entry, const std::string& profileId) {
        bindProfile(entry, profileId);
    };
    tray_.add(hwnd_, WM_APP_TRAY);
    // explorer.exe 重启后托盘图标会消失，收到 TaskbarCreated 时重挂
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    applyEnabledState();

    // 首次启动：打开设置窗口，让用户立刻完成配置
    Settings& s = Settings::shared();
    if (!s.hasCompletedFirstLaunch) {
        openSettings();
    }

    updatechecker::CheckIfDue(hwnd_, WM_APP_UPDATE);

    // 配置的 Ollama 模型未安装时自动挑一个已装的（避免开箱即败）。
    // 网络在工作线程，Settings 的读写回到主线程（WM_APP_MODELS）。
    ollamamodels::FetchInstalledAsync(hwnd_, WM_APP_MODELS);

    return true;
}

void App::applyEnabledState() {
    UnregisterHotKey(hwnd_, kReadHotkeyId);
    UnregisterHotKey(hwnd_, kRewriteHotkeyId);
    SelectionMonitor::shared().stop();
    SelectionIcon::shared().hide();
    readShortcutRegistered_ = false;
    rewriteShortcutRegistered_ = false;
    shortcutsReady_ = false;

    const Settings& s = Settings::shared();
    if (!s.enabled) return;

    if (s.readShortcut.isConfigured() && s.rewriteShortcut.isConfigured()
        && s.readShortcut == s.rewriteShortcut) {
        diagnostics::Record("shortcutRegistration", "failure", {}, {}, "duplicate");
        if (!hotkeyWarningShown_) {
            hotkeyWarningShown_ = true;
            MessageBoxW(nullptr,
                        L"Translate and rewrite must use different shortcuts. Choose a different combination in Settings → Shortcuts.",
                        L"TypeTide — duplicate shortcuts", MB_OK | MB_ICONWARNING);
        }
        return;
    }

    const bool readConfigured = s.readShortcut.isConfigured();
    const bool rewriteConfigured = s.rewriteShortcut.isConfigured();
    bool ok1 = !readConfigured || RegisterHotKey(hwnd_, kReadHotkeyId,
                                                 s.readShortcut.modifiers | MOD_NOREPEAT,
                                                 s.readShortcut.vk);
    bool ok2 = !rewriteConfigured || RegisterHotKey(hwnd_, kRewriteHotkeyId,
                                                    s.rewriteShortcut.modifiers | MOD_NOREPEAT,
                                                    s.rewriteShortcut.vk);
    readShortcutRegistered_ = ok1;
    rewriteShortcutRegistered_ = ok2;
    shortcutsReady_ = (readConfigured || rewriteConfigured) && ok1 && ok2;
    diagnostics::Record("shortcutRegistration", shortcutsReady_ ? "success" : "failure",
                        {}, {}, shortcutsReady_ ? "" :
                                  (!readConfigured && !rewriteConfigured ? "notConfigured" : "unavailable"));
    if ((!ok1 || !ok2) && !hotkeyWarningShown_) {
        hotkeyWarningShown_ = true;
        std::wstring unavailable;
        if (readConfigured && !ok1) unavailable += L"Translate selection: " + s.readShortcut.displayString();
        if (!ok1 && !ok2) unavailable += L"\n";
        if (rewriteConfigured && !ok2) unavailable += L"Rewrite & replace: " + s.rewriteShortcut.displayString();
        std::wstring message = L"These shortcuts could not be registered:\n\n" + unavailable +
                               L"\n\nChoose different shortcuts in Settings → Shortcuts.";
        MessageBoxW(nullptr, message.c_str(),
                    L"TypeTide — shortcut unavailable", MB_OK | MB_ICONWARNING);
    }
    if (ok1 && ok2) hotkeyWarningShown_ = false;

    setupSelectionTrigger();
}

void App::setupSelectionTrigger() {
    const Settings& s = Settings::shared();
    if (s.selectionTrigger == SelectionTrigger::None) return;

    SelectionMonitor::shared().onSelection = [this](const capture::Capture& cap, POINT mouse) {
        const Settings& st = Settings::shared();
        if (!st.enabled || foregroundAppSkipped()) return;
        if (st.selectionTrigger == SelectionTrigger::Icon) {
            capture::Capture copy = cap;
            // 点击图标才算触发：此刻冻结划词入口绑定的 profile
            SelectionIcon::shared().show(cap.anchor, cap.hasAnchor, mouse, [this, copy] {
                presentRead(copy, beginTrigger(ProfileEntry::Selection, ShortcutAction::SmartPopup).context);
            });
        } else if (st.selectionTrigger == SelectionTrigger::Auto) {
            presentRead(cap, beginTrigger(ProfileEntry::Selection, ShortcutAction::SmartPopup).context);
        }
    };
    SelectionMonitor::shared().start();
}

void App::openSettings() {
    SettingsWindow::open(hwnd_);
}

void App::quit() {
    // AppExit 也取消没有弹窗的当前会话（直接替换、已点 Replace 的粘贴）：它们的粘贴会在
    // 下一道守卫处放弃并还原剪贴板。
    PopupWindow::shared().close(session::CloseReason::AppExit);
    cancelRequests(ledger_.close(ledger_.current(), session::CloseReason::AppExit));
    UnregisterHotKey(hwnd_, kReadHotkeyId);
    UnregisterHotKey(hwnd_, kRewriteHotkeyId);
    SelectionIcon::shared().hide();
    SelectionMonitor::shared().stop();
    translator::CancelAll();
    tray_.remove();
    finishQuit();
}

void App::finishQuit() {
    // 已发出 Ctrl+V 的替换还在等目标取数据（≤2s）时，先让它还原用户剪贴板。UI 线程只
    // try_lock 轮询，从不等待这把锁；超时仍会退出。
    if (!clipboard::TransactionMutex().try_lock() && quitAttempts_++ < kQuitRetries) {
        SetTimer(hwnd_, kQuitTimerId, kQuitRetryMs, nullptr);
        return;
    }
    // 直接结束进程：分离的工作线程（流式翻译/替换）不再有机会
    // 在静态对象析构后继续跑（那是退出崩溃的经典来源）。
    ExitProcess(0);
}

// ---------- 跳过列表 ----------

bool App::foregroundAppSkipped() const {
    const Settings& s = Settings::shared();
    if (s.skipApps.empty()) return false;

    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return false;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return false;
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    bool got = QueryFullProcessImageNameW(proc, 0, path, &size);
    CloseHandle(proc);
    if (!got) return false;

    std::wstring exe = path;
    auto slash = exe.find_last_of(L"\\/");
    if (slash != std::wstring::npos) exe = exe.substr(slash + 1);
    std::wstring exeLower = util::ToLower(exe);
    std::wstring exeNoExt = exeLower;
    if (exeNoExt.size() > 4 && exeNoExt.rfind(L".exe") == exeNoExt.size() - 4)
        exeNoExt.resize(exeNoExt.size() - 4);

    for (const auto& entry : s.skipApps) {
        std::wstring e = util::ToLower(util::Trim(entry));
        if (e.empty()) continue;
        if (e.size() > 4 && e.rfind(L".exe") == e.size() - 4) e.resize(e.size() - 4);
        if (e == exeNoExt) return true;
    }
    return false;
}

// ---------- 触发：快捷键槽位 / 划词 → 绑定的 profile ----------

// 取词之前冻结：取词期间保存的设置只影响之后的触发；更新的触发让尚在取词的旧触发作废。
App::Trigger App::beginTrigger(ProfileEntry entry, ShortcutAction action) {
    return {translator::FreezeEntry(entry, action), ++triggerSeq_, Settings::shared().rewritePreview};
}

void App::performEntry(ProfileEntry entry, ShortcutAction action) {
    // 动作决定弹窗还是替换
    Trigger trigger = beginTrigger(entry, action);
    if (IsReplaceAction(action)) handleRewrite(std::move(trigger));
    else handleRead(std::move(trigger));
}

void App::handleRead(Trigger trigger) {
    const Settings& s = Settings::shared();
    util::Log("handleRead enabled=%d", s.enabled);
    if (!s.enabled || foregroundAppSkipped()) return;

    // UIA 快路径在主线程（无睡眠）；拿不到再去工作线程走剪贴板兜底
    // （兜底要等最多 400ms，主线程睡了低级钩子会被系统摘掉）
    if (auto cap = capture::UIAReadSelection()) {
        diagnostics::Record("selectionCapture", "success", {}, "uia", {}, -1, -1,
                            (int)cap->text.size());
        presentRead(*cap, std::move(trigger.context));
        return;
    }
    captureFallbackAsync(WM_APP_READ_CAPTURED, std::move(trigger));
}

void App::handleRewrite(Trigger trigger) {
    const Settings& s = Settings::shared();
    if (!s.enabled || foregroundAppSkipped()) return;

    // 与 handleRead 同理：UIA 主线程快路径，剪贴板兜底进工作线程
    if (auto cap = capture::UIACaptureForRewrite()) {
        diagnostics::Record("selectionCapture", "success", {}, "uia", {}, -1, -1,
                            (int)cap->text.size());
        proceedRewrite(*cap, std::move(trigger));
        return;
    }
    captureFallbackAsync(WM_APP_REWRITE_CAPTURED, std::move(trigger));
}

void App::captureFallbackAsync(UINT message, Trigger trigger) {
    HWND hwnd = hwnd_;
    const HWND target = GetForegroundWindow();  // 触发时的前台窗口：只从它复制、只粘贴回它
    std::thread([hwnd, message, target, trigger = std::move(trigger)] {
        auto copied = capture::ClipboardFallbackCopy(target);
        if (!copied) {
            diagnostics::Record("selectionCapture", "failure");
            return;
        }
        diagnostics::Record("selectionCapture", "success", {}, "clipboardFallback", {}, -1, -1,
                            (int)copied->size());
        auto* captured = new CapturedSelection;
        captured->cap.text = *copied;
        captured->cap.anchor = capture::CursorAnchor();
        captured->cap.hasAnchor = true;
        captured->cap.window = target;
        captured->trigger = trigger;
        if (!PostMessageW(hwnd, message, 0, (LPARAM)captured)) delete captured;
    }).detach();
}

bool App::rejectIfTooLong(const capture::Capture& cap, const execution::ContextPtr& context) {
    if (cap.text.size() <= kMaxInputChars) return false;
    beginUserTranslation();  // 被拒绝的触发也是新的用户操作：之前未落下的粘贴不再执行
    RECT anchor = cap.hasAnchor ? cap.anchor : capture::CursorAnchor();
    wchar_t msg[128];
    swprintf_s(msg, L"Selection is too long (%zu characters, max %zu)",
               cap.text.size(), kMaxInputChars);
    showErrorPopup(cap.text.substr(0, 80), context ? context->profile.nativeLanguage : Language::English,
                   anchor, msg);
    return true;
}

uint64_t App::beginUserTranslation() {
    session::Ledger::Started started = ledger_.beginSession();
    cancelRequests(started.cancel);
    return started.session;
}

void App::cancelRequests(const std::vector<uint64_t>& requestIds) {
    for (uint64_t id : requestIds) translator::Cancel(id);
}

// ---------- 读·弹窗会话（含改写预览）----------

void App::presentRead(const capture::Capture& cap, execution::ContextPtr context) {
    if (rejectIfTooLong(cap, context)) return;
    openPopupSession(cap, std::move(context), /*replaceSelectAll=*/false);
}

void App::openPopupSession(const capture::Capture& cap, execution::ContextPtr context, bool replaceSelectAll) {
    RECT anchor = cap.hasAnchor ? cap.anchor : capture::CursorAnchor();
    if (!context) {
        beginUserTranslation();
        showErrorPopup(cap.text, Language::English, anchor, kNoProfileError);
        return;
    }
    const uint64_t sessionId = beginUserTranslation();
    const Language target =
        execution::ResolveActionDirection(context->profile, context->action, cap.text).target;
    popupSession_ = sessionId;
    sessionContext_ = std::move(context);
    sessionText_ = cap.text;
    sessionWindow_ = cap.window;
    currentReplaceSelectAll_ = replaceSelectAll;

    // show() 先以 Superseded 关掉旧弹窗；旧会话此时已过期，它的关闭不会影响新会话。
    PopupWindow::shared().show(cap.text, target, anchor, /*showReplace=*/true);
    installPopupHandlers(sessionId);
    startStream(std::nullopt);
}

void App::installPopupHandlers(uint64_t sessionId) {
    PopupWindow& popup = PopupWindow::shared();
    popup.onReplace = [this, sessionId] { onPopupReplace(sessionId); };
    popup.onRetarget = [this](Language newTarget) {
        PopupWindow& p = PopupWindow::shared();
        p.setTarget(newTarget);
        p.resetForRetranslate();
        startStream(newTarget);  // 同一冻结 profile，只换目标语言；上一次尝试被取消
    };
    popup.onRetry = [this] {
        PopupWindow& p = PopupWindow::shared();
        p.resetForRetranslate();
        startStream(p.target());
    };
    popup.onClosed = [this, sessionId](session::CloseReason reason) {
        cancelRequests(ledger_.close(sessionId, reason));  // 只影响这个弹窗自己的会话
    };
}

void App::onPopupReplace(uint64_t sessionId) {
    if (sessionId != popupSession_ || !sessionContext_) return;
    // custom 原样交付（缩进/制表符/结尾换行），preset 去首尾空白；空结果不替换
    std::optional<std::wstring> text =
        execution::DeliveredOutput(sessionContext_->profile, PopupWindow::shared().translation());
    if (!text) return;
    session::PasteGuard guard =
        ledger_.pasteGuard(sessionId, reinterpret_cast<std::uintptr_t>(sessionWindow_));
    const bool selectAll = currentReplaceSelectAll_;
    PopupWindow::shared().close(session::CloseReason::ReplaceCommit);
    replacer::ReplaceAsync(std::move(*text), selectAll, std::move(guard));
}

void App::startStream(std::optional<Language> targetOverride) {
    session::Ledger::Attempt attempt = ledger_.beginAttempt(popupSession_);
    cancelRequests(attempt.cancel);
    if (!ledger_.accepts(attempt.ticket)) return;  // 会话已关闭或已被取代
    HWND hwnd = hwnd_;
    const session::Ticket ticket = attempt.ticket;
    // 回调只带创建时的 ticket；同步的缓存命中也只是 PostMessage，晚于 attachRequest 处理。
    const uint64_t requestId = translator::Stream(
        sessionContext_, sessionText_, targetOverride,
        [hwnd, ticket](uint64_t, const std::wstring& delta) {
            auto* payload = new DeltaMsg{ticket, delta};
            if (!PostMessageW(hwnd, WM_APP_TR_DELTA, 0, (LPARAM)payload)) delete payload;
        },
        [hwnd, ticket](uint64_t, bool ok, const std::wstring& error) {
            auto* payload = new DoneMsg{ticket, ok, error};
            if (!PostMessageW(hwnd, WM_APP_TR_DONE, 0, (LPARAM)payload)) delete payload;
        });
    ledger_.attachRequest(ticket, requestId);
}

// ---------- 写·输入改写 ----------

void App::proceedRewrite(const capture::Capture& cap, Trigger trigger) {
    if (rejectIfTooLong(cap, trigger.context)) return;
    // 先预览：弹窗显示译文 + Replace（整框改写替换时先全选）。开关取触发时的值。
    if (trigger.preview) {
        openPopupSession(cap, std::move(trigger.context), cap.isWholeField);
        return;
    }
    replaceDirectly(cap, std::move(trigger.context));
}

void App::replaceDirectly(const capture::Capture& cap, execution::ContextPtr context) {
    RECT anchor = cap.hasAnchor ? cap.anchor : capture::CursorAnchor();
    if (!context) {
        beginUserTranslation();
        showErrorPopup(cap.text, Language::English, anchor, kNoProfileError);
        return;
    }
    // 旧弹窗会话被取代：关掉它，免得停在半截译文上还能点 Replace
    PopupWindow::shared().close(session::CloseReason::Superseded);
    const uint64_t sessionId = beginUserTranslation();
    const session::Ticket ticket = ledger_.beginAttempt(sessionId).ticket;
    // 直接替换：异步流结束后回主线程，确认会话仍是最新再交给 replacer（它在按键前还会再查）。
    const Language target =
        execution::ResolveActionDirection(context->profile, context->action, cap.text).target;
    struct RewriteStreamState {
        std::mutex mutex;
        std::wstring result;
    };
    auto state = std::make_shared<RewriteStreamState>();
    HWND hwnd = hwnd_;
    const bool wholeField = cap.isWholeField;
    const HWND window = cap.window;
    const std::wstring original = cap.text;
    const uint64_t requestId = translator::Stream(
        context, cap.text, std::nullopt,
        [state](uint64_t, const std::wstring& delta) {
            std::lock_guard lock(state->mutex);
            state->result += delta;
        },
        [hwnd, state, context, ticket, wholeField, anchor, window, original, target](
            uint64_t, bool ok, const std::wstring& error) {
            std::lock_guard lock(state->mutex);
            std::optional<std::wstring> text =
                ok ? execution::DeliveredOutput(context->profile, state->result) : std::nullopt;
            auto* msg = new RewriteResult{
                ticket,
                text.has_value(),
                text ? std::move(*text) : std::wstring(),
                text ? std::wstring() : (ok ? std::wstring(L"Empty translation") : error),
                wholeField,
                anchor,
                window,
                original,
                target,
            };
            if (!PostMessageW(hwnd, WM_APP_REWRITE_DONE, 0, (LPARAM)msg)) delete msg;
        });
    ledger_.attachRequest(ticket, requestId);
}

void App::showErrorPopup(const std::wstring& original, Language target, RECT anchor, const std::wstring& error) {
    PopupWindow& popup = PopupWindow::shared();
    popup.show(original, target, anchor, /*showReplace=*/false);
    popup.onReplace = nullptr;
    popup.onRetarget = nullptr;
    popup.onRetry = nullptr;
    popup.onClosed = nullptr;
    popup.setError(error);
}

// ---------- 托盘绑定 ----------

void App::bindProfile(ProfileEntry entry, const std::string& profileId) {
    std::string error;
    if (profilebinding::Commit(Settings::shared(), entry, profileId, &error)) return;  // 只影响之后的触发
    const std::wstring message = L"The profile binding could not be changed.\n\n" + util::Widen(error);
    MessageBoxW(nullptr, message.c_str(), L"TypeTide", MB_OK | MB_ICONWARNING);
}

// ---------- 消息处理 ----------

void App::onHotkey(WPARAM id) {
    // 物理槽位决定 profile，槽位配置的动作决定方向与弹窗/替换
    const Settings& s = Settings::shared();
    if (id == kReadHotkeyId) performEntry(ProfileEntry::Shortcut1, s.readShortcutAction);
    else if (id == kRewriteHotkeyId) performEntry(ProfileEntry::Shortcut2, s.rewriteShortcutAction);
}

void App::onTrayEvent(UINT event) {
    if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP || event == WM_LBUTTONUP)
        tray_.showMenu(hwnd_);
    else if (event == WM_LBUTTONDBLCLK)
        openSettings();
}

void App::onDelta(const DeltaMsg& msg) {
    PopupWindow& popup = PopupWindow::shared();
    if (ledger_.accepts(msg.ticket) && popup.isVisible()) popup.appendDelta(msg.delta);
}

void App::onTranslationDone(const DoneMsg& done) {
    PopupWindow& popup = PopupWindow::shared();
    if (!ledger_.accepts(done.ticket) || !popup.isVisible()) return;
    if (!done.ok)
        popup.setError(done.error);
    else if (!sessionContext_ || !execution::DeliveredOutput(sessionContext_->profile, popup.translation()))
        popup.setError(L"No translation returned");
    else
        popup.finishTranslation();
}

void App::onRewriteDone(RewriteResult& result) {
    if (!ledger_.accepts(result.ticket)) return;  // 已有更新的用户翻译：不再粘贴
    if (result.ok) {
        replacer::ReplaceAsync(std::move(result.text), result.isWholeField,
                               ledger_.pasteGuard(result.ticket.session,
                                                  reinterpret_cast<std::uintptr_t>(result.window)));
    } else {
        showErrorPopup(result.original, result.target, result.anchor, result.error);
    }
}

void App::onModelsListed(const std::vector<std::wstring>& models) {
    // 工作线程拉回的已装模型列表；Settings 的比对与保存在主线程做
    ollamamodels::ApplyResolvedModels(models);
    ollamamodels::PreloadAsync();
}

void App::onCaptured(UINT message, CapturedSelection& captured) {
    if (captured.trigger.seq != triggerSeq_) return;  // 取词晚到，期间已有更新的触发
    if (message == WM_APP_READ_CAPTURED)
        presentRead(captured.cap, std::move(captured.trigger.context));
    else
        proceedRewrite(captured.cap, std::move(captured.trigger));
}

LRESULT App::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HOTKEY:
        onHotkey(wp);
        return 0;
    case WM_APP_TRAY:
        onTrayEvent(LOWORD(lp));
        return 0;
    case WM_APP_TR_DELTA:
        onDelta(*std::unique_ptr<DeltaMsg>((DeltaMsg*)lp));
        return 0;
    case WM_APP_TR_DONE:
        onTranslationDone(*std::unique_ptr<DoneMsg>((DoneMsg*)lp));
        return 0;
    case WM_APP_REWRITE_DONE:
        onRewriteDone(*std::unique_ptr<RewriteResult>((RewriteResult*)lp));
        return 0;
    case WM_APP_UPDATE:
        tray_.setUpdateAvailable(true);
        return 0;
    case WM_APP_OPEN_SETTINGS:
        openSettings();
        return 0;
    case WM_APP_MODELS:
        onModelsListed(*std::unique_ptr<std::vector<std::wstring>>((std::vector<std::wstring>*)lp));
        return 0;
    case WM_APP_READ_CAPTURED:
    case WM_APP_REWRITE_CAPTURED:
        onCaptured(msg, *std::unique_ptr<CapturedSelection>((CapturedSelection*)lp));
        return 0;
    case WM_TIMER:
        if (wp != kQuitTimerId) break;
        finishQuit();
        return 0;
    case WM_DESTROY:
        tray_.remove();
        return 0;
    }

    // explorer 重启 → 托盘图标丢失，重挂
    if (taskbarCreatedMsg_ && msg == taskbarCreatedMsg_) {
        tray_.remove();
        tray_.add(hwnd_, WM_APP_TRAY);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
