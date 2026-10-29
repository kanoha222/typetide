#include "Clipboard.h"

namespace clipboard {

namespace {
/// 打开剪贴板（短重试，其他进程可能占用）
struct Opener {
    bool ok = false;
    Opener() {
        for (int i = 0; i < 5; ++i) {
            if (OpenClipboard(nullptr)) { ok = true; return; }
            Sleep(10);
        }
    }
    ~Opener() { if (ok) CloseClipboard(); }
};
} // namespace

/// Text of the already opened clipboard.
std::optional<std::wstring> readOpen() {
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) return std::nullopt;
    auto* p = static_cast<const wchar_t*>(GlobalLock(h));
    if (!p) return std::nullopt;
    std::wstring text(p);
    GlobalUnlock(h);
    return text;
}

std::optional<std::wstring> GetText() {
    Opener open;
    if (!open.ok) return std::nullopt;
    return readOpen();
}

bool SetText(const std::wstring& text) {
    Opener open;
    if (!open.ok) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) return false;
    void* p = GlobalLock(h);
    if (!p) { GlobalFree(h); return false; }
    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(h);
    if (!SetClipboardData(CF_UNICODETEXT, h)) {
        GlobalFree(h);
        return false;
    }
    return true;
}

DWORD SequenceNumber() {
    return GetClipboardSequenceNumber();
}

Snapshot Take() {
    Opener open;
    if (!open.ok) return Snapshot{std::nullopt, false};
    return Snapshot{readOpen(), true};
}

std::mutex& TransactionMutex() {
    static std::mutex mutex;
    return mutex;
}

void Restore(const Snapshot& snap) {
    if (!snap.readable) return;  // never empty a clipboard we could not read
    if (snap.text) {
        SetText(*snap.text);
    } else {
        Opener open;
        if (open.ok) EmptyClipboard();
    }
}

} // namespace clipboard
