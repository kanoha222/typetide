#include "TranslationExecutionSupport.h"
#include <initializer_list>
#include <string>
#include <vector>

// Prepare-level endpoint policy for OpenAI-compatible and Ollama profiles.
// Builder rules (query/fragment, host+port joining, loopback-only HTTP for OpenAI)
// are tested here; the raw authority matrix lives in the classifier tests.
namespace execution_test {
namespace {

using execution::PrepareStatus;
using execution::PreparedRequest;

PreparedRequest OpenAIWith(const char* base, const char* key) {
    execution::ConnectionSnapshot connection = MakeConnection();
    connection.openAIBaseURL = base;
    connection.openAIKey = key;
    return RunPrepare(MakeProfile(TranslationBackend::OpenAI), ShortcutAction::SmartPopup, connection,
                      kEnglishText);
}

PreparedRequest OllamaWith(const char* host, int port) {
    execution::ConnectionSnapshot connection = MakeConnection();
    connection.ollamaHost = host;
    connection.ollamaPort = port;
    return RunPrepare(MakeProfile(TranslationBackend::Ollama), ShortcutAction::SmartPopup, connection,
                      kEnglishText);
}

bool IsRejected(PrepareStatus status) {
    return status == PrepareStatus::EndpointInvalid || status == PrepareStatus::EndpointInsecure;
}

void TestOpenAILoopbackHttp() {
    const char* const bases[] = {"http://localhost:1234/v1", "http://127.0.0.1:1234/v1",
                                 "http://[::1]:1234/v1", "http://LOCALHOST:1234/v1"};
    for (const char* base : bases) {
        for (const char* key : {"", kFakeKey}) {
            const PreparedRequest r = OpenAIWith(base, key);
            if (!CheckPrepared(r, "OpenAI loopback HTTP is accepted with or without a key")) continue;
            Check(r.finalUrl == AsciiWiden(base) + L"/chat/completions",
                  "OpenAI loopback final URL is the exact base plus /chat/completions");
        }
    }
}

void TestOpenAIRemoteHttpRejected() {
    const char* const clean[] = {"http://api.example.test/v1", "http://192.168.1.5:1234/v1",
                                 "http://localhost.evil.test/v1"};
    for (const char* base : clean) {
        for (const char* key : {"", kFakeKey}) {
            Check(OpenAIWith(base, key).status == PrepareStatus::EndpointInsecure,
                  "OpenAI remote HTTP is refused whether or not a key is set");
        }
    }
    const char* const lookalikes[] = {"http://127.0.0.2/v1", "http://127.1/v1", "http://2130706433/v1",
                                      "http://[0:0:0:0:0:0:0:1]/v1", "http://[::ffff:127.0.0.1]/v1",
                                      "http://127.0.0.1./v1"};
    for (const char* base : lookalikes) {
        Check(IsRejected(OpenAIWith(base, "").status), "OpenAI loopback look-alike is rejected (no key)");
        Check(IsRejected(OpenAIWith(base, kFakeKey).status), "OpenAI loopback look-alike is rejected (key)");
    }
}

void TestOpenAIMalformedBases() {
    const char* const bad[] = {"http://user@localhost/v1", "http://localhost@evil.test/v1",
                               "https://user:pw@api.example.test/v1", "https://api.example.test:0/v1",
                               "https://api.example.test:65536/v1", "https://api.example.test/v1?x=1",
                               "https://api.example.test/v1#f", "ftp://api.example.test",
                               "api.example.test/v1", "https://api.example.test\\v1",
                               "https://api.%65xample.test/v1", "https://api.example.test /v1"};
    for (const char* base : bad)
        Check(OpenAIWith(base, kFakeKey).status == PrepareStatus::EndpointInvalid,
              "OpenAI malformed or query/fragment base is EndpointInvalid");
    Check(OpenAIWith("", kFakeKey).status == PrepareStatus::EndpointMissing,
          "OpenAI empty base is EndpointMissing");
}

void TestOpenAIKeyAndUrlBuilding() {
    Check(OpenAIWith("https://api.example.test/v1", "").status == PrepareStatus::KeyMissing,
          "OpenAI HTTPS without a key is KeyMissing");
    const struct { const char* base; const wchar_t* url; } rows[] = {
        {"https://api.example.test/v1/", L"https://api.example.test/v1/chat/completions"},
        {"https://api.example.test/v1///", L"https://api.example.test/v1/chat/completions"},
        {"https://api.example.test", L"https://api.example.test/chat/completions"},
        {"https://API.Example.test/v1", L"https://API.Example.test/v1/chat/completions"},
        {"https://api.example.test/v1%2Fx", L"https://api.example.test/v1%2Fx/chat/completions"},
    };
    for (const auto& row : rows) {
        const PreparedRequest r = OpenAIWith(row.base, kFakeKey);
        if (CheckPrepared(r, "OpenAI valid HTTPS base prepares"))
            Check(r.finalUrl == row.url, "OpenAI final URL: trailing slashes trimmed, host case and percent escapes kept");
    }
}

void TestOllamaUrlBuilding() {
    const struct { const char* host; int port; const wchar_t* url; } rows[] = {
        {"http://127.0.0.1", 11434, L"http://127.0.0.1:11434/api/generate"},
        {"http://localhost", 8080, L"http://localhost:8080/api/generate"},
        {"http://[::1]", 11434, L"http://[::1]:11434/api/generate"},
        {"https://ollama.example.test", 443, L"https://ollama.example.test:443/api/generate"},
        {"http://192.168.1.5", 11434, L"http://192.168.1.5:11434/api/generate"},
    };
    for (const auto& row : rows) {
        const PreparedRequest r = OllamaWith(row.host, row.port);
        if (CheckPrepared(r, "Ollama host + port prepares (remote HTTP stays allowed, no key)"))
            Check(r.finalUrl == row.url, "Ollama final URL is host:port/api/generate");
    }
}

void TestOllamaRejections() {
    const struct { const char* host; int port; } bad[] = {
        {"http://127.0.0.1:11434", 11434}, {"http://127.0.0.1:9999", 11434},
        {"http://127.0.0.1/proxy", 11434}, {"http://127.0.0.1?x=1", 11434},
        {"http://user@127.0.0.1", 11434},  {"http://127.0.0.1%2f", 11434},
        {"http://127.0.0 .1", 11434},      {"127.0.0.1", 11434},
        {"http://127.0.0.1", 0},           {"http://127.0.0.1", 65536},
        {"http://127.0.0.1", -1},
    };
    for (const auto& row : bad)
        Check(OllamaWith(row.host, row.port).status == PrepareStatus::EndpointInvalid,
              "Ollama explicit-port clash, path, query, userinfo or bad port is EndpointInvalid");
    Check(OllamaWith("", 11434).status == PrepareStatus::EndpointMissing, "Ollama empty host is EndpointMissing");
}

} // namespace

void RunPrepareEndpointTests() {
    TestOpenAILoopbackHttp();
    TestOpenAIRemoteHttpRejected();
    TestOpenAIMalformedBases();
    TestOpenAIKeyAndUrlBuilding();
    TestOllamaUrlBuilding();
    TestOllamaRejections();
}

} // namespace execution_test
