#include "TextReplacer.h"
#include "Clipboard.h"
#include "Keyboard.h"
#include "SelectionCapture.h"
#include "LocalDiagnostics.h"
#include "Util.h"
#include <cstdint>
#include <mutex>
#include <thread>

namespace replacer {

namespace {

constexpr wchar_t kOwnerClass[] = L"TypeTideClipboardOwner";

struct RenderContext {
    const std::wstring* text = nullptr;
    bool rendered = false;
};

void serveText(const std::wstring& text) {
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) return;
    if (void* p = GlobalLock(h)) {
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(h);
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    } else {
        GlobalFree(h);
    }
}

LRESULT CALLBACK ownerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = (RenderContext*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_RENDERFORMAT:
        // 目标应用 GetClipboardData → 粘贴真正发生了
        if (ctx && ctx->text && wp == CF_UNICODETEXT) {
            serveText(*ctx->text);
            ctx->rendered = true;
        }
        return 0;
    case WM_RENDERALLFORMATS:
        if (ctx && ctx->text && OpenClipboard(hwnd)) {
            if (GetClipboardOwner() == hwnd) serveText(*ctx->text);
            CloseClipboard();
            ctx->rendered = true;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/// Final input-commit boundary: the session must still be current and the captured window
/// still in front. Checked before the clipboard is touched and before every injected key.
bool mayCommit(const session::PasteGuard& guard, ReplaceOutcome* outcome) {
    const HWND foreground = GetForegroundWindow();
    if (guard.allows(reinterpret_cast<std::uintptr_t>(foreground),
                     reinterpret_cast<std::uintptr_t>(capture::FocusedControl(foreground))))
        return true;
    *outcome = guard.isCurrent() ? ReplaceOutcome::AbortedTargetChanged : ReplaceOutcome::AbortedStale;
    return false;
}

/// One paste attempt's clipboard state. The destructor restores the user's clipboard on every
/// path (paste, abort, failure) and only then destroys the delayed-render owner.
class PasteTransaction {
public:
    explicit PasteTransaction(const std::wstring& text) : saved_(clipboard::Take()) {
        ctx_.text = &text;
        static bool registered = [] {
            WNDCLASSW wc{};
            wc.lpfnWndProc = ownerProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kOwnerClass;
            RegisterClassW(&wc);
            return true;
        }();
        (void)registered;
        owner_ = CreateWindowExW(0, kOwnerClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        if (!saved_.readable) return;  // could not save the user's clipboard: touch nothing
        if (owner_) {
            SetWindowLongPtrW(owner_, GWLP_USERDATA, (LONG_PTR)&ctx_);
            if (OpenClipboard(owner_)) {
                EmptyClipboard();
                SetClipboardData(CF_UNICODETEXT, nullptr);  // 延迟渲染
                CloseClipboard();
                delayed_ = GetClipboardOwner() == owner_;
            }
        }
        if (!delayed_) {
            written_ = clipboard::SetText(text);  // 兜底：老式直接写 + 固定延迟
            sequence_ = clipboard::SequenceNumber();
        }
    }

    ~PasteTransaction() {
        clipboard::Restore(saved_);  // a no-op when the snapshot could not be taken
        if (owner_) {
            SetWindowLongPtrW(owner_, GWLP_USERDATA, 0);
            DestroyWindow(owner_);
        }
    }

    PasteTransaction(const PasteTransaction&) = delete;
    PasteTransaction& operator=(const PasteTransaction&) = delete;

    /// Our text is on the clipboard (delayed or written) and the user's clipboard was saved.
    bool ready() const { return saved_.readable && (delayed_ || written_); }

    /// Right before Ctrl+V: nobody replaced our clipboard content since it was set.
    bool stillOurs() const {
        return delayed_ ? GetClipboardOwner() == owner_ : clipboard::SequenceNumber() == sequence_;
    }

    /// After Ctrl+V: true when the target actually read the clipboard (or rendering is direct).
    bool waitForPaste() {
        if (!delayed_) {
            Sleep(150);
            return true;
        }
        // 泵消息等 WM_RENDERFORMAT；2s 没人取说明粘贴没发生（目标不收粘贴等）
        DWORD deadline = GetTickCount() + 2000;
        MSG m;
        while (!ctx_.rendered) {
            DWORD now = GetTickCount();
            if (now >= deadline) break;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, deadline - now, QS_ALLINPUT);
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        util::Log("replace rendered=%d", ctx_.rendered);
        // 渲染后小宽限：有些目标会紧接着再读一次
        if (ctx_.rendered) Sleep(150);
        return ctx_.rendered;
    }

private:
    clipboard::Snapshot saved_;
    RenderContext ctx_;
    HWND owner_ = nullptr;
    bool delayed_ = false;
    bool written_ = false;
    DWORD sequence_ = 0;
};

/// Do not inject Ctrl+V until the triggering gesture has finished: the tail of the popup's
/// click, or the key-ups of a shortcut still held when a fast (cached) result arrives
/// (Ctrl+Shift+R), can reach the editor between the Ctrl and V events and leave a literal "v"
/// behind or change the combination. False when it is still held after 2 s.
bool waitForGestureRelease() {
    const int keys[] = {VK_LBUTTON, VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN};
    auto held = [&keys] {
        for (int key : keys)
            if (GetAsyncKeyState(key) & 0x8000) return true;
        return false;
    };
    const DWORD deadline = GetTickCount() + 2000;
    while (held()) {
        if (GetTickCount() >= deadline) return false;
        Sleep(5);
    }
    Sleep(20);
    return true;
}

ReplaceOutcome paste(const std::wstring& text, bool selectAll, const session::PasteGuard& guard) {
    ReplaceOutcome outcome = ReplaceOutcome::Pasted;
    if (!mayCommit(guard, &outcome)) return outcome;  // nothing touched yet
    PasteTransaction transaction(text);
    // Ctrl+V without our text on the clipboard would paste whatever the user copied last.
    if (!transaction.ready()) return ReplaceOutcome::ClipboardUnavailable;
    if (selectAll) {
        if (!mayCommit(guard, &outcome)) return outcome;
        keyboard::SendSelectAll();
        Sleep(40);
    }
    if (!mayCommit(guard, &outcome)) return outcome;
    if (!transaction.stillOurs()) return ReplaceOutcome::ClipboardUnavailable;
    keyboard::SendPaste();  // from here the input cannot be taken back
    return transaction.waitForPaste() ? ReplaceOutcome::Pasted : ReplaceOutcome::PasteNotConsumed;
}

const char* failureReason(ReplaceOutcome outcome) {
    switch (outcome) {
    case ReplaceOutcome::Pasted: return "";
    case ReplaceOutcome::PasteNotConsumed: return "pasteNotConsumed";
    case ReplaceOutcome::AbortedStale: return "superseded";
    case ReplaceOutcome::AbortedTargetChanged: return "targetChanged";
    case ReplaceOutcome::ClipboardUnavailable: return "clipboardUnavailable";
    case ReplaceOutcome::TriggerStillPressed: return "triggerStillPressed";
    }
    return "unknown";
}

/// 工作线程主体：等点击/快捷键松开 → 守卫 → 延迟渲染剪贴板 → 守卫 → 合成粘贴 → 等目标取数据 → 还原剪贴板
ReplaceOutcome replaceWorker(const std::wstring& text, bool selectAll, const session::PasteGuard& guard) {
    std::lock_guard lock(clipboard::TransactionMutex());  // never overlaps a fallback copy capture
    const DWORD started = GetTickCount();
    const ReplaceOutcome outcome =
        waitForGestureRelease() ? paste(text, selectAll, guard) : ReplaceOutcome::TriggerStillPressed;
    const bool ok = outcome == ReplaceOutcome::Pasted;
    diagnostics::Record("replacement", ok ? "success" : "failure", {}, {}, failureReason(outcome),
                        -1, (int)(GetTickCount() - started), (int)text.size());
    return outcome;
}

} // namespace

void ReplaceAsync(std::wstring text, bool selectAll, session::PasteGuard guard) {
    std::thread([text = std::move(text), selectAll, guard = std::move(guard)] {
        replaceWorker(text, selectAll, guard);
    }).detach();
}

ReplaceOutcome ReplaceForTesting(const std::wstring& text, bool selectAll, const session::PasteGuard& guard) {
    return replaceWorker(text, selectAll, guard);
}

} // namespace replacer
