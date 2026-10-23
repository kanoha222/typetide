#include "EndpointPolicy.h"
#include "HttpUrl.h"
#include <cstddef>

namespace execution {

namespace {

wchar_t AsciiLower(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

bool EqualsNoCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (AsciiLower(a[i]) != AsciiLower(b[i])) return false;
    return true;
}

bool StartsWithNoCase(const std::wstring& text, const wchar_t* prefix) {
    const std::wstring p(prefix);
    return text.size() >= p.size() && EqualsNoCase(text.substr(0, p.size()), p);
}

bool IsAsciiAlnum(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
}

bool IsHexDigit(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

// Printable ASCII only: no whitespace, controls, NUL, DEL, non-ASCII or backslash anywhere.
bool HasOnlySafeCharacters(const std::wstring& url) {
    for (wchar_t c : url)
        if (c <= 0x20 || c >= 0x7F || c == L'\\') return false;
    return true;
}

bool IsRegName(const std::wstring& host) {
    if (host.empty()) return false;
    for (wchar_t c : host)
        if (!IsAsciiAlnum(c) && c != L'-' && c != L'.' && c != L'_' && c != L'~') return false;
    return true;
}

bool IsBracketedIpv6(const std::wstring& host) {
    if (host.size() < 4 || host.front() != L'[' || host.back() != L']') return false;
    for (std::size_t i = 1; i + 1 < host.size(); ++i)
        if (!IsHexDigit(host[i]) && host[i] != L':' && host[i] != L'.') return false;
    return host.find(L':') != std::wstring::npos;
}

// 1..65535, decimal digits only (no sign, no empty text).
bool ParsePort(const std::wstring& text, unsigned* port) {
    if (text.empty() || text.size() > 5) return false;
    unsigned value = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
        value = value * 10 + static_cast<unsigned>(c - L'0');
    }
    if (value < 1 || value > 65535) return false;
    *port = value;
    return true;
}

struct Authority {
    std::wstring host;
    std::wstring portText;
    bool hasPort = false;
};

bool SplitAuthority(const std::wstring& authority, Authority* out) {
    if (authority.empty() || authority.find_first_of(L"@%") != std::wstring::npos) return false;
    std::size_t hostEnd = 0;
    if (authority.front() == L'[') {
        const std::size_t close = authority.find(L']');
        if (close == std::wstring::npos) return false;
        hostEnd = close + 1;
        out->host = authority.substr(0, hostEnd);
        if (!IsBracketedIpv6(out->host)) return false;
    } else {
        if (authority.find_first_of(L"[]") != std::wstring::npos) return false;
        hostEnd = authority.find(L':');
        if (hostEnd == std::wstring::npos) hostEnd = authority.size();
        out->host = authority.substr(0, hostEnd);
        if (!IsRegName(out->host)) return false;
    }
    if (hostEnd == authority.size()) return true;
    if (authority[hostEnd] != L':') return false;
    out->hasPort = true;
    out->portText = authority.substr(hostEnd + 1);
    return true;
}

bool IsLoopbackHost(const std::wstring& host) {
    return EqualsNoCase(host, L"localhost") || host == L"127.0.0.1" || host == L"[::1]";
}

// The transport later cracks the same string with WinHTTP; refuse any disagreement.
bool AgreesWithWinHttp(const EndpointInfo& info, bool secure) {
    const http::UrlParts parts = http::CrackUrl(info.url);
    return parts.ok && parts.secure == secure && parts.port == info.port &&
           EqualsNoCase(parts.host, info.host);
}

} // namespace

EndpointInfo ClassifyEndpoint(const std::wstring& url) {
    EndpointInfo info;
    info.url = url;
    info.kind = EndpointKind::Invalid;
    if (!HasOnlySafeCharacters(url)) return info;

    bool secure = false;
    std::size_t start = 0;
    if (StartsWithNoCase(url, L"http://")) {
        start = 7;
    } else if (StartsWithNoCase(url, L"https://")) {
        secure = true;
        start = 8;
    } else {
        return info;
    }

    const std::size_t end = url.find_first_of(L"/?#", start);
    Authority authority;
    if (!SplitAuthority(url.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start),
                        &authority))
        return info;
    unsigned port = secure ? 443u : 80u;
    if (authority.hasPort && !ParsePort(authority.portText, &port)) return info;

    info.host = authority.host;
    info.port = port;
    info.explicitPort = authority.hasPort;
    info.hasPath = end != std::wstring::npos && url[end] == L'/';
    info.hasQueryOrFragment = end != std::wstring::npos && url.find_first_of(L"?#", end) != std::wstring::npos;
    if (!AgreesWithWinHttp(info, secure)) return info;

    if (secure) info.kind = EndpointKind::Https;
    else if (IsLoopbackHost(info.host)) info.kind = EndpointKind::LoopbackHttp;
    else info.kind = EndpointKind::RemoteHttp;
    return info;
}

} // namespace execution
