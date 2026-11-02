#include "Http.h"
#include "HttpUrl.h"
#include <windows.h>
#include <winhttp.h>
#include <wchar.h>
#include <vector>

namespace http {

namespace {

constexpr size_t kMaxBufferedResponse = 8 * 1024 * 1024;
constexpr size_t kMaxStreamedResponse = 16 * 1024 * 1024;  // far above any translation

/// Loopback traffic (local Ollama / API servers) never goes through a configured proxy.
bool isLoopbackHost(const std::wstring& host) {
    return _wcsicmp(host.c_str(), L"localhost") == 0 || host == L"127.0.0.1" || host == L"[::1]" ||
           host == L"::1";
}

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET v) : h(v) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

std::string lastError(const char* stage) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s failed (code %lu)", stage, GetLastError());
    return buf;
}

Result request(const std::wstring& url,
               const wchar_t* method,
               const std::string& body,
               const std::wstring& extraHeaders,
               const std::function<void(const char*, size_t)>* onChunk,
               const std::atomic<bool>* cancel,
               bool followRedirects) {
    Result r;

    // Same offline parse as the endpoint classifier, with no fixed host/path buffers.
    const UrlParts parts = CrackUrl(url);
    if (!parts.ok) {
        r.error = "Invalid URL";
        return r;
    }
    const std::wstring object = (parts.path.empty() ? std::wstring(L"/") : parts.path) + parts.extra;

    const DWORD access = isLoopbackHost(parts.host) ? WINHTTP_ACCESS_TYPE_NO_PROXY
                                                    : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
    Handle session(WinHttpOpen(L"TypeTide/1.0", access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { r.error = lastError("WinHttpOpen"); return r; }

    // 连接失败应尽快可恢复；单次 90 秒无响应后让 UI 提供 Retry。
    WinHttpSetTimeouts(session.h, 10000, 10000, 30000, 90000);

    Handle conn(WinHttpConnect(session.h, parts.host.c_str(), static_cast<INTERNET_PORT>(parts.port), 0));
    if (!conn) { r.error = lastError("WinHttpConnect"); return r; }

    Handle req(WinHttpOpenRequest(conn.h, method, object.c_str(), nullptr,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  parts.secure ? WINHTTP_FLAG_SECURE : 0));
    if (!req) { r.error = lastError("WinHttpOpenRequest"); return r; }

    // Provider requests carry credentials and prompts: a 3xx is reported, never followed.
    if (!followRedirects) {
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) {
            r.error = lastError("Redirect policy");
            return r;
        }
    }

    std::wstring headers = L"Content-Type: application/json\r\n" + extraHeaders;
    if (!WinHttpSendRequest(req.h, headers.c_str(), (DWORD)headers.size(),
                            body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                            (DWORD)body.size(), (DWORD)body.size(), 0)) {
        r.error = lastError("Connection");
        return r;
    }
    if (!WinHttpReceiveResponse(req.h, nullptr)) {
        r.error = lastError("Response");
        return r;
    }

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    std::vector<char> buf(16 * 1024);
    size_t streamed = 0;
    for (;;) {
        if (cancel && cancel->load()) { r.error = "Cancelled"; return r; }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) { r.error = lastError("Read"); return r; }
        if (avail == 0) break;
        while (avail > 0) {
            if (cancel && cancel->load()) { r.error = "Cancelled"; return r; }
            DWORD toRead = avail < buf.size() ? avail : (DWORD)buf.size();
            DWORD got = 0;
            if (!WinHttpReadData(req.h, buf.data(), toRead, &got)) { r.error = lastError("Read"); return r; }
            if (got == 0) break;
            avail -= got;
            if (onChunk && status >= 200 && status < 300) {
                streamed += got;
                if (streamed > kMaxStreamedResponse) {
                    r.error = "Response too large";
                    return r;
                }
                (*onChunk)(buf.data(), got);
            } else {
                if (r.body.size() + got > kMaxBufferedResponse) {
                    r.error = "Response too large";
                    return r;
                }
                r.body.append(buf.data(), got);
            }
        }
    }

    if (status < 200 || status >= 300) {
        char msg[64];
        snprintf(msg, sizeof(msg), "HTTP %lu", status);
        r.error = msg;
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace

Result PostStream(const std::wstring& url, const std::string& body,
                  const std::wstring& extraHeaders,
                  const std::function<void(const char*, size_t)>& onChunk,
                  const std::atomic<bool>& cancel) {
    return request(url, L"POST", body, extraHeaders, &onChunk, &cancel, /*followRedirects=*/false);
}

Result Get(const std::wstring& url, const std::wstring& extraHeaders, bool followRedirects) {
    return request(url, L"GET", {}, extraHeaders, nullptr, nullptr, followRedirects);
}

} // namespace http
