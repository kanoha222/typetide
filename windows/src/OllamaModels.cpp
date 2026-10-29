#include "OllamaModels.h"
#include "Http.h"
#include "Json.h"
#include "Settings.h"
#include "TranslationExecution.h"
#include "Util.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <initializer_list>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace ollamamodels {

namespace {
std::atomic<uint64_t> g_preloadGeneration{0};

double parameterBillions(const std::wstring& model) {
    std::wstring lower = util::ToLower(model);
    size_t b = lower.find(L'b');
    while (b != std::wstring::npos) {
        size_t start = b;
        while (start > 0 && (iswdigit(lower[start - 1]) || lower[start - 1] == L'.')) --start;
        if (start < b && (start == 0 || lower[start - 1] == L':' || lower[start - 1] == L'_' || lower[start - 1] == L'-')) {
            wchar_t* end = nullptr;
            double value = std::wcstod(lower.c_str() + start, &end);
            if (end == lower.c_str() + b && value > 0) return value;
        }
        b = lower.find(L'b', b + 1);
    }
    return -1;
}
} // namespace

std::vector<std::wstring> ListInstalled(const std::string& host, int port, std::wstring* error) {
    const std::optional<std::wstring> base = execution::OllamaBase(host, port);
    if (!base) {
        if (error) *error = L"Invalid Ollama host. Use http(s)://host only and set the port separately.";
        return {};
    }
    http::Result r = http::Get(*base + L"/api/tags", L"", /*followRedirects=*/false);
    if (!r.ok) {
        if (error)
            *error = r.status == 0 ? L"Can't reach Ollama — is `ollama serve` running?"
                                   : util::Widen(r.error);
        return {};
    }
    bool ok = false;
    json::Value v = json::Parse(r.body, &ok);
    if (!ok) {
        if (error) *error = L"Unexpected response from Ollama";
        return {};
    }
    std::vector<std::wstring> models;
    if (const json::Array* arr = v["models"].array())
        for (auto& m : *arr)
            if (m["name"].isString()) models.push_back(util::Widen(m["name"].asString()));
    return models;
}

void FetchInstalledAsync(HWND notify, UINT message, profilestatus::Ticket ticket) {
    // 已提交 Settings 的快照在调用（主）线程拍好，工作线程只碰局部拷贝
    const SettingsSnapshot snapshot = Settings::shared().snapshot();
    std::string host = snapshot.ollamaHost;
    int port = snapshot.ollamaPort;

    std::thread([notify, message, ticket, host, port] {
        auto* discovery = new Discovery;
        discovery->ticket = ticket;
        discovery->models = ListInstalled(host, port, &discovery->error);
        discovery->ok = discovery->error.empty();
        if (!PostMessageW(notify, message, 0, (LPARAM)discovery)) delete discovery;
    }).detach();
}

void PreloadAsync(const std::vector<std::wstring>* installed) {
    // Only models that committed profiles actually bind, each once; never the whole library and
    // never a substitute for a missing model.
    const uint64_t mine = ++g_preloadGeneration;
    const SettingsSnapshot snapshot = Settings::shared().snapshot();
    const std::vector<std::string> models = profilestatus::PreloadModels(snapshot.profileConfig, installed);
    const std::optional<std::wstring> base = execution::OllamaBase(snapshot.ollamaHost, snapshot.ollamaPort);
    if (models.empty() || !base) return;
    const std::wstring url = *base + L"/api/generate";

    std::thread([url, models, mine] {
        for (const std::string& model : models) {
            if (g_preloadGeneration.load() != mine) return;  // bindings or connection changed since
            for (const bool think : {true, false}) {  // models that reject "think" answer HTTP 400
                json::Object body;
                body["model"] = model;
                body["prompt"] = "";
                body["stream"] = false;
                body["keep_alive"] = "10m";
                if (think) body["think"] = false;
                std::atomic<bool> cancel{false};
                http::Result r = http::PostStream(url, json::Value(std::move(body)).dump(), L"",
                                                  [](const char*, size_t) {}, cancel);
                if (r.ok || r.status != 400 || !think) {
                    if (!r.ok) util::Log("ollama preload failed: %s", r.error.c_str());
                    break;
                }
            }
        }
    }).detach();
}

void CancelPreload() {
    ++g_preloadGeneration;
}

void OnDiscovered(const std::vector<std::wstring>& installed) {
    PreloadAsync(&installed);
}

std::wstring RecommendedForMemory(const std::vector<std::wstring>& installed,
                                  uint64_t physicalMemoryBytes,
                                  double* maximumParameterBillions) {
    if (installed.empty()) return {};
    const double gib = (double)physicalMemoryBytes / 1073741824.0;
    const double cap = gib < 8 ? 3 : gib < 16 ? 7 : gib < 32 ? 14 : 32;
    if (maximumParameterBillions) *maximumParameterBillions = cap;

    const std::wstring* best = nullptr;
    double bestSize = -1;
    const std::wstring* smallest = nullptr;
    double smallestSize = std::numeric_limits<double>::max();
    for (const auto& model : installed) {
        const double size = parameterBillions(model);
        if (size <= 0) continue;
        if (size < smallestSize) { smallest = &model; smallestSize = size; }
        if (size <= cap && size > bestSize) { best = &model; bestSize = size; }
    }
    return best ? *best : smallest ? *smallest : installed.front();
}

} // namespace ollamamodels
