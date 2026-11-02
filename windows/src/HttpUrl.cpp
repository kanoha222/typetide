#include "HttpUrl.h"
#include <windows.h>
#include <winhttp.h>

namespace http {

namespace {

std::wstring Component(const wchar_t* text, DWORD length) {
    return text && length ? std::wstring(text, length) : std::wstring();
}

} // namespace

UrlParts CrackUrl(const std::wstring& url) {
    UrlParts parts;
    if (url.empty() || url.size() > MAXDWORD || url.find(L'\0') != std::wstring::npos) return parts;

    // Lengths of -1 with null buffers make WinHttpCrackUrl return pointers into `url`
    // (no copies, no fixed buffers, no escaping or decoding).
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUserNameLength = static_cast<DWORD>(-1);
    uc.dwPasswordLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc)) return parts;
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) return parts;

    parts.secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    parts.host = Component(uc.lpszHostName, uc.dwHostNameLength);
    // An IPv6 literal must keep its brackets so host:port joins stay unambiguous.
    if (parts.host.find(L':') != std::wstring::npos && parts.host.front() != L'[')
        parts.host = L"[" + parts.host + L"]";
    parts.port = uc.nPort;
    parts.path = Component(uc.lpszUrlPath, uc.dwUrlPathLength);
    parts.extra = Component(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    parts.ok = !parts.host.empty();
    return parts;
}

} // namespace http
