#include "Translator.h"
#include "BuiltInModel.h"
#include "Http.h"
#include "Json.h"
#include "LocalDiagnostics.h"
#include "Settings.h"
#include "Util.h"
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace translator {

namespace {

using Clock = std::chrono::steady_clock;

// ---------- LRU 缓存（key = execution::PreparedRequest::cacheIdentity）----------

class Cache {
public:
    static Cache& shared() {
        static Cache c;
        return c;
    }

    std::optional<std::wstring> get(const std::string& key) {
        std::lock_guard lock(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        order_.splice(order_.end(), order_, it->second.orderIt);
        return it->second.value;
    }

    void set(const std::string& key, const std::wstring& value) {
        std::lock_guard lock(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second.value = value;
            order_.splice(order_.end(), order_, it->second.orderIt);
            return;
        }
        order_.push_back(key);
        map_[key] = {value, std::prev(order_.end())};
        while (map_.size() > kCapacity) {
            map_.erase(order_.front());
            order_.pop_front();
        }
    }

    void clear() {
        std::lock_guard lock(mu_);
        map_.clear();
        order_.clear();
    }

private:
    static constexpr size_t kCapacity = 200;
    struct Entry {
        std::wstring value;
        std::list<std::string>::iterator orderIt;
    };
    std::mutex mu_;
    std::unordered_map<std::string, Entry> map_;
    std::list<std::string> order_;
};

// ---------- 流式行组装（NDJSON / SSE 都按行切）----------

class LineAssembler {
public:
    /// 喂入一块字节，产出完整行（不含换行符）
    void feed(const char* data, size_t len, const std::function<void(const std::string&)>& onLine) {
        // 容忍流最前面的 UTF-8 BOM
        if (buf_.empty() && !bomChecked_ && len >= 3 &&
            (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
            data += 3;
            len -= 3;
        }
        bomChecked_ = true;
        buf_.append(data, len);
        size_t pos;
        while ((pos = buf_.find('\n')) != std::string::npos) {
            std::string line = buf_.substr(0, pos);
            buf_.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) onLine(line);
        }
    }

    void finish(const std::function<void(const std::string&)>& onLine) {
        if (!buf_.empty()) {
            if (buf_.back() == '\r') buf_.pop_back();
            if (!buf_.empty()) onLine(buf_);
            buf_.clear();
        }
    }

private:
    std::string buf_;
    bool bomChecked_ = false;
};

// ---------- 活跃请求管理 ----------

struct ActiveRequest {
    std::shared_ptr<std::atomic<bool>> cancel;
};

std::mutex g_mu;
std::unordered_map<uint64_t, ActiveRequest> g_active;
std::atomic<uint64_t> g_nextId{1};

void finishRequest(uint64_t id) {
    std::lock_guard lock(g_mu);
    g_active.erase(id);
}

// ---------- provider 实现（工作线程内运行）----------
// 工作线程绝不读 Settings::shared()：每个请求带着主线程冻结的 context 与 Prepare 结果。

struct Job {
    execution::ContextPtr context;
    execution::PreparedRequest request;
    std::wstring text;
};

struct StreamOutcome {
    bool ok = false;
    std::wstring error;
    std::wstring full;
};

std::wstring providerError(const http::Result& result) {
    if (!result.body.empty()) {
        bool ok = false;
        json::Value body = json::Parse(result.body, &ok);
        if (ok) {
            const json::Value& nested = body["error"];
            if (nested.isObject() && nested["message"].isString())
                return util::Widen(nested["message"].asString());
            if (nested.isString()) return util::Widen(nested.asString());
            if (body["message"].isString()) return util::Widen(body["message"].asString());
        }
    }
    return util::Widen(result.error);
}

StreamOutcome runOllama(const Job& job, uint64_t id, const DeltaFn& onDelta, const std::atomic<bool>& cancel) {
    StreamOutcome out;
    LineAssembler lines;
    auto onLine = [&](const std::string& line) {
        bool ok = false;
        json::Value v = json::Parse(line, &ok);
        if (!ok) return;
        const std::string& delta = v["response"].asString();
        if (!delta.empty()) {
            std::wstring wdelta = util::Widen(delta);
            out.full += wdelta;
            onDelta(id, wdelta);
        }
    };
    auto onChunk = [&](const char* data, size_t len) { lines.feed(data, len, onLine); };

    const execution::PreparedRequest& request = job.request;
    http::Result r = http::PostStream(request.finalUrl, request.body, L"", onChunk, cancel);
    if (r.status == 400 && !cancel.load()) {
        // 思考型模型需 think:false；不支持该字段的模型会 400 → 用不含 think 的 retry body 重试
        util::Log("ollama: think rejected (400), retrying without");
        lines = LineAssembler();
        out.full.clear();
        r = http::PostStream(request.finalUrl, request.retryBody, L"", onChunk, cancel);
    }
    if (!r.ok) {
        out.error = r.status == 0 && !cancel.load()
                        ? L"Can't reach Ollama — is `ollama serve` running?"
                        : util::Widen(r.error);
        return out;
    }
    lines.finish(onLine);
    out.ok = true;
    return out;
}

std::wstring openAIHeaders(const Job& job) {
    std::wstring headers;
    const std::string& key = job.context->connection.openAIKey;
    if (!key.empty()) headers += L"Authorization: Bearer " + util::Widen(key) + L"\r\n";
    if (job.request.finalUrl.find(L"openrouter.ai") != std::wstring::npos) {
        headers += L"HTTP-Referer: https://xnu.app/TypeTide/\r\n";
        headers += L"X-OpenRouter-Title: TypeTide\r\n";
    }
    return headers;
}

StreamOutcome runOpenAI(const Job& job, uint64_t id, const DeltaFn& onDelta, const std::atomic<bool>& cancel) {
    StreamOutcome out;
    LineAssembler lines;
    auto onLine = [&](const std::string& line) {
        if (auto delta = ParseOpenAISSELine(line)) {
            out.full += *delta;
            onDelta(id, *delta);
        }
    };
    auto onChunk = [&](const char* data, size_t len) { lines.feed(data, len, onLine); };

    http::Result r = http::PostStream(job.request.finalUrl, job.request.body, openAIHeaders(job), onChunk, cancel);
    if (!r.ok) {
        out.error = providerError(r);
        return out;
    }
    lines.finish(onLine);
    out.ok = true;
    return out;
}

StreamOutcome runBuiltIn(const Job& job, uint64_t id, const DeltaFn& onDelta, const std::atomic<bool>& cancel) {
    // Prepare only lets the Faithful preset through to BuiltIn.
    const TranslationRequest request{job.text, job.request.direction.source, job.request.direction.target,
                                     RewriteStyle::Faithful};
    StreamOutcome out;
    out.ok = builtin::Stream(request, id, onDelta, cancel, &out.full, &out.error);
    return out;
}

StreamOutcome runProvider(const Job& job, uint64_t id, const DeltaFn& onDelta, const std::atomic<bool>& cancel) {
    switch (job.request.backend) {
    case TranslationBackend::BuiltIn: return runBuiltIn(job, id, onDelta, cancel);
    case TranslationBackend::OpenAI: return runOpenAI(job, id, onDelta, cancel);
    case TranslationBackend::Ollama: return runOllama(job, id, onDelta, cancel);
    }
    return {};
}

const char* backendName(TranslationBackend backend) {
    switch (backend) {
    case TranslationBackend::BuiltIn: return "builtin-translategemma-q4km";
    case TranslationBackend::OpenAI: return "openai";
    case TranslationBackend::Ollama: return "ollama";
    }
    return "unknown";
}

int elapsedMs(Clock::time_point from, Clock::time_point to) {
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

// Caches and reports the delivered text: exact for custom prompts, trimmed for presets.
void completeJob(const Job& job, uint64_t id, const StreamOutcome& out, const DoneFn& onDone,
                 int firstMs, int totalMs) {
    const char* backend = backendName(job.request.backend);
    const int chars = (int)job.text.size();
    const std::optional<std::wstring> delivered =
        out.ok ? execution::DeliveredOutput(job.context->profile, out.full) : std::nullopt;
    if (delivered) {
        Cache::shared().set(job.request.cacheIdentity, *delivered);
        onDone(id, true, L"");
        diagnostics::Record("translation", "success", backend, {}, {}, firstMs, totalMs, chars);
        return;
    }
    onDone(id, false, out.ok ? L"Empty translation" : out.error);
    diagnostics::Record("translation", "failure", backend, {}, out.ok ? "emptyResponse" : "provider",
                        firstMs, totalMs, chars);
}

void runJob(const Job& job, uint64_t id, const std::shared_ptr<std::atomic<bool>>& cancel,
            Clock::time_point start, const DeltaFn& onDelta, const DoneFn& onDone) {
    std::optional<Clock::time_point> firstToken;
    DeltaFn trackedDelta = [&](uint64_t requestId, const std::wstring& delta) {
        if (!delta.empty() && !firstToken) firstToken = Clock::now();
        onDelta(requestId, delta);
    };
    const StreamOutcome out = runProvider(job, id, trackedDelta, *cancel);
    const bool cancelled = cancel->load();
    finishRequest(id);
    const int totalMs = elapsedMs(start, Clock::now());
    const int firstMs = firstToken ? elapsedMs(start, *firstToken) : -1;
    if (cancelled) {
        diagnostics::Record("translation", "cancelled", backendName(job.request.backend), {}, "cancelled",
                            firstMs, totalMs, (int)job.text.size());
        return;  // 取消的请求不再回调
    }
    completeJob(job, id, out, onDone, firstMs, totalMs);
}

// The Backend tab's connection test checks the connection and model it is editing, with a
// fixed synthetic sentence; it never uses a selection or a saved profile.
execution::ContextPtr healthCheckContext() {
    const Settings& s = Settings::shared();
    TranslationProfile probe;
    probe.id = "connection-test";
    probe.name = "Connection test";
    probe.nativeLanguage = Language::Chinese;
    probe.foreignLanguage = Language::English;
    probe.direction = TranslationDirection::ForeignToNative;
    probe.backend = s.backend;
    probe.model = s.backend == TranslationBackend::Ollama ? s.ollamaModel
                : s.backend == TranslationBackend::OpenAI ? s.openAIModel : std::string();
    execution::ConnectionSnapshot connection;
    connection.connectionEpoch = s.connectionEpoch();
    connection.ollamaHost = s.ollamaHost;
    connection.ollamaPort = s.ollamaPort;
    connection.openAIBaseURL = s.openAIBaseURL;
    connection.openAIKey = s.openAIKey;
    return execution::Freeze(std::move(probe), ShortcutAction::SmartPopup, std::move(connection));
}

} // namespace

std::string SystemPrompt(Language target, std::optional<Language> source, RewriteStyle style) {
    return execution::PresetSystemPrompt(target, source, style);
}

std::string PlainPrompt(const std::wstring& text, Language target, std::optional<Language> source, RewriteStyle style) {
    return execution::PresetPlainPrompt(text, target, source, style);
}

execution::ConnectionSnapshot ConnectionFrom(const SettingsSnapshot& snapshot) {
    execution::ConnectionSnapshot connection;
    connection.connectionEpoch = snapshot.connectionEpoch;
    connection.ollamaHost = snapshot.ollamaHost;
    connection.ollamaPort = snapshot.ollamaPort;
    connection.openAIBaseURL = snapshot.openAIBaseURL;
    connection.openAIKey = snapshot.openAIKey;
    return connection;
}

execution::ContextPtr FreezeEntry(ProfileEntry entry, ShortcutAction action) {
    const SettingsSnapshot snapshot = Settings::shared().snapshot();
    return execution::FreezeBound(snapshot.profileConfig, entry, action, ConnectionFrom(snapshot));
}

uint64_t Stream(const execution::ContextPtr& context, const std::wstring& text,
                std::optional<Language> targetOverride, DeltaFn onDelta, DoneFn onDone) {
    const uint64_t id = g_nextId.fetch_add(1);
    if (!context) {
        onDone(id, false, L"No translation profile is bound to this action.");
        return id;
    }
    const Clock::time_point start = Clock::now();
    const char* provider = backendName(context->profile.backend);
    execution::PreparedRequest request = execution::Prepare(*context, text, targetOverride);
    if (!request.ok()) {
        diagnostics::Record("translation", "failure", provider, {}, "prepare", -1, 0, (int)text.size());
        onDone(id, false, execution::PrepareErrorMessage(request.status, request.backend));
        return id;
    }
    // 缓存命中：同步回调全文（缓存里存的已是交付文本）
    if (auto cached = Cache::shared().get(request.cacheIdentity)) {
        onDelta(id, *cached);
        onDone(id, true, L"");
        diagnostics::Record("translation", "cacheHit", provider, {}, {}, 0, 0, (int)text.size());
        return id;
    }

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard lock(g_mu);
        g_active[id] = {cancel};
    }
    std::thread([job = Job{context, std::move(request), text}, id, cancel, start,
                 onDelta = std::move(onDelta), onDone = std::move(onDone)] {
        runJob(job, id, cancel, start, onDelta, onDone);
    }).detach();
    return id;
}

void Cancel(uint64_t reqId) {
    std::lock_guard lock(g_mu);
    auto it = g_active.find(reqId);
    if (it != g_active.end()) it->second.cancel->store(true);
}

void CancelAll() {
    std::lock_guard lock(g_mu);
    for (auto& [id, req] : g_active) req.cancel->store(true);
}

std::optional<std::wstring> TranslateFully(const execution::ContextPtr& context, const std::wstring& text,
                                           std::wstring* error) {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false, ok = false;
    std::wstring full, err;

    Stream(
        context, text, std::nullopt,
        [&](uint64_t, const std::wstring& delta) {
            std::lock_guard lock(mu);
            full += delta;
        },
        [&](uint64_t, bool success, const std::wstring& e) {
            std::lock_guard lock(mu);
            ok = success;
            err = e;
            done = true;
            cv.notify_all();
        });

    std::unique_lock lock(mu);
    cv.wait(lock, [&] { return done; });

    const std::optional<std::wstring> delivered =
        ok && context ? execution::DeliveredOutput(context->profile, full) : std::nullopt;
    if (!delivered && error) *error = ok ? L"Empty translation" : err;
    return delivered;
}

void CheckHealthAsync(std::function<void(HealthCheckResult)> completion) {
    const auto start = Clock::now();
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    struct State {
        std::mutex mutex;
        std::wstring full;
        std::optional<Clock::time_point> first;
    };
    auto state = std::make_shared<State>();
    const execution::ContextPtr context = healthCheckContext();
    const std::string backend = backendName(context->profile.backend);
    Stream(
        context, L"TypeTide health check " + std::to_wstring(stamp), std::nullopt,
        [state](uint64_t, const std::wstring& delta) {
            std::lock_guard lock(state->mutex);
            if (!delta.empty() && !state->first) state->first = Clock::now();
            state->full += delta;
        },
        [state, start, backend, completion = std::move(completion)](
            uint64_t, bool success, const std::wstring& message) mutable {
            std::lock_guard lock(state->mutex);
            HealthCheckResult result;
            result.ok = success && !util::Trim(state->full).empty();
            result.message = result.ok ? L"Connected and translated successfully."
                                       : (message.empty() ? L"No translation returned." : message);
            result.firstTokenMs = state->first ? elapsedMs(start, *state->first) : -1;
            result.totalMs = elapsedMs(start, Clock::now());
            diagnostics::Record("backendHealthCheck", result.ok ? "success" : "failure",
                                backend, {}, result.ok ? "" : "backend",
                                result.firstTokenMs, result.totalMs);
            completion(std::move(result));
        });
}

void CheckConnectionAsync(const profilestatus::Probe&, const execution::ConnectionSnapshot&,
                          std::function<void(HealthCheckResult)> completion) {
    // RED scaffold: not implemented yet.
    HealthCheckResult result;
    result.message = L"Connection tests are not implemented yet.";
    completion(std::move(result));
}

void ClearCacheForTesting() {
    Cache::shared().clear();
}

std::optional<std::wstring> ParseOpenAISSELine(const std::string& line) {
    if (line.rfind("data:", 0) != 0) return std::nullopt;
    const std::string payload = util::Trim(line.substr(5));
    if (payload == "[DONE]") return std::nullopt;
    bool ok = false;
    json::Value value = json::Parse(payload, &ok);
    if (!ok) return std::nullopt;
    const json::Value& delta = value["choices"].at(0)["delta"]["content"];
    if (!delta.isString() || delta.asString().empty()) return std::nullopt;
    return util::Widen(delta.asString());
}

std::vector<std::string> AssembleLinesForTesting(const std::vector<std::string>& fragments) {
    LineAssembler assembler;
    std::vector<std::string> lines;
    auto append = [&](const std::string& line) { lines.push_back(line); };
    for (const auto& fragment : fragments) assembler.feed(fragment.data(), fragment.size(), append);
    assembler.finish(append);
    return lines;
}

} // namespace translator
