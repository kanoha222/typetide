#include "TextReplacer.h"
#include "Clipboard.h"
#include "Keyboard.h"
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
    if (guard.allows(reinterpret_cast<std::uintptr_t>(GetForegroundWindow()))) return true;
    // allows(target) only fails when the session itself is no longer current.
    *outcome = guard.allows(guard.target) ? ReplaceOutcome::AbortedTargetChanged : ReplaceOutcome::AbortedStale;
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
        if (owner_) {
            SetWindowLongPtrW(owner_, GWLP_USERDATA, (LONG_PTR)&ctx_);
            if (OpenClipboard(owner_)) {
                EmptyClipboard();
                SetClipboardData(CF_UNICODETEXT, nullptr);  // 延迟渲染
                CloseClipboard();
                delayed_ = true;
            }
        }
        if (!delayed_) clipboard::SetText(text);  // 兜底：老式直接写 + 固定延迟
    }

    ~PasteTransaction() {
        clipboard::Restore(saved_);
        if (owner_) {
            SetWindowLongPtrW(owner_, GWLP_USERDATA, 0);
            DestroyWindow(owner_);
        }
    }

    PasteTransaction(const PasteTransaction&) = delete;
    PasteTransaction& operator=(const PasteTransaction&) = delete;

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
};

void waitForClickRelease() {
    // Replace can be started by the popup's WM_LBUTTONDOWN handler.  Do not
    // inject Ctrl+V until that physical click has completed: otherwise the
    // foreground editor can process the tail of the click between the Ctrl
    // and V events, leaving a literal "v" behind on some controls.
    DWORD mouseDeadline = GetTickCount() + 250;
    while ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && GetTickCount() < mouseDeadline)
        Sleep(5);
    Sleep(20);
}

ReplaceOutcome paste(const std::wstring& text, bool selectAll, const session::PasteGuard& guard) {
    ReplaceOutcome outcome = ReplaceOutcome::Pasted;
    if (!mayCommit(guard, &outcome)) return outcome;  // nothing touched yet
    PasteTransaction transaction(text);
    if (selectAll) {
        if (!mayCommit(guard, &outcome)) return outcome;
        keyboard::SendSelectAll();
        Sleep(40);
    }
    if (!mayCommit(guard, &outcome)) return outcome;
    keyboard::SendPaste();  // from here the input cannot be taken back
    return transaction.waitForPaste() ? ReplaceOutcome::Pasted : ReplaceOutcome::PasteNotConsumed;
}

const char* failureReason(ReplaceOutcome outcome) {
    switch (outcome) {
    case ReplaceOutcome::Pasted: return "";
    case ReplaceOutcome::PasteNotConsumed: return "pasteNotConsumed";
    case ReplaceOutcome::AbortedStale: return "superseded";
    case ReplaceOutcome::AbortedTargetChanged: return "targetChanged";
    }
    return "unknown";
}

/// 工作线程主体：等点击结束 → 守卫 → 延迟渲染剪贴板 → 守卫 → 合成粘贴 → 等目标取数据 → 还原剪贴板
ReplaceOutcome replaceWorker(const std::wstring& text, bool selectAll, const session::PasteGuard& guard) {
    std::lock_guard lock(clipboard::TransactionMutex());  // never overlaps a fallback copy capture
    const DWORD started = GetTickCount();
    waitForClickRelease();
    const ReplaceOutcome outcome = paste(text, selectAll, guard);
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
