//
//  EndpointPolicy.h — pure, offline authority classifier for provider URLs.
//
//  It classifies the EXACT wide string the transport will later receive
//  (EndpointInfo::url is that string, never a normalized copy). It performs no
//  DNS, no percent-decoding of the authority and no network access. Builder
//  rules (query/fragment rejection, Ollama host+port joining, OpenAI/Ollama
//  policy) live in TranslationExecution, not here.
//
#pragma once
#include <string>

namespace execution {

enum class EndpointKind {
    NotReady,      // scaffold state: classifier not implemented yet
    Invalid,       // malformed or unsafe authority / unsupported scheme
    LoopbackHttp,  // http:// and host is exactly localhost / 127.0.0.1 / [::1]
    Https,         // https:// with a syntactically valid authority
    RemoteHttp,    // http:// with any other syntactically valid authority
};

struct EndpointInfo {
    EndpointKind kind = EndpointKind::NotReady;
    std::wstring url;          // the exact input, unchanged
    std::wstring host;         // as written; IPv6 keeps brackets, e.g. L"[::1]"
    unsigned port = 0;         // explicit port, otherwise 80 / 443
    bool explicitPort = false;
    bool hasPath = false;      // a non-empty path (including a lone "/")
    bool hasQueryOrFragment = false;
};

/// Authority rules: ASCII only; reject userinfo '@', '%', '\\', whitespace,
/// controls and NUL; port 1..65535 digits; loopback HTTP is exactly the three
/// host tokens, case-insensitive. Path percent-escapes are allowed.
EndpointInfo ClassifyEndpoint(const std::wstring& url);

} // namespace execution
