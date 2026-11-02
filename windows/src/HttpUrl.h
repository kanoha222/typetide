//
//  HttpUrl.h — offline URL cracking shared by the endpoint classifier and, in
//  the later runtime phase, by the WinHTTP transport.
//
//  CrackUrl never opens a session, connects or sends anything: it only wraps
//  WinHttpCrackUrl. Components are read through borrowed pointers into the
//  input and copied into value strings, so there is no fixed host/path buffer
//  limit (the old Http.cpp request() capped them at 256/2048 wide chars).
//
#pragma once
#include <string>

namespace http {

struct UrlParts {
    bool ok = false;
    bool secure = false;     // https
    std::wstring host;       // IPv6 literals keep their brackets, e.g. L"[::1]"
    unsigned port = 0;       // explicit port or the scheme default (80 / 443)
    std::wstring path;       // path only, e.g. L"/v1/chat"
    std::wstring extra;      // query/fragment tail exactly as cracked, e.g. L"?x=1"
};

/// Offline parse of one http/https URL. ok == false for anything WinHTTP rejects.
UrlParts CrackUrl(const std::wstring& url);

} // namespace http
