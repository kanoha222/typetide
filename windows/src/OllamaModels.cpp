#include "OllamaModels.h"
#include "Http.h"
#include "Json.h"
#include "Settings.h"
#include "Util.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace ollamamodels {

namespace {
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
    std::wstring url = util::Widen(host) + L":" + std::to_wstring(port) + L"/api/tags";

    http::Result r = http::Get(url);
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
    static std::atomic<uint64_t> generation{0};
    const uint64_t mine = ++generation;
    const SettingsSnapshot snapshot = Settings::shared().snapshot();
    const std::vector<std::string> models = profilestatus::PreloadModels(snapshot.profileConfig, installed);
    if (models.empty()) return;
    const std::wstring url =
        util::Widen(snapshot.ollamaHost) + L":" + std::to_wstring(snapshot.ollamaPort) + L"/api/generate";

    std::thread([url, models, mine] {
        for (const std::string& model : models) {
            if (generation.load() != mine) return;  // bindings or connection changed since
            json::Object body;
            body["model"] = model;
            body["prompt"] = "";
            body["stream"] = false;
            body["keep_alive"] = "10m";
            body["think"] = false;
            std::atomic<bool> cancel{false};
            http::Result r = http::PostStream(url, json::Value(std::move(body)).dump(), L"",
                                              [](const char*, size_t) {}, cancel);
            if (!r.ok) util::Log("ollama preload failed: %s", r.error.c_str());
        }
    }).detach();
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
