//
//  Translator.h — 翻译服务：执行冻结的 profile（BuiltIn / Ollama / OpenAI 兼容，流式）、LRU 缓存。
//  对应 macOS 的 TranslationService + providers。请求内容由 execution::Prepare 决定。
//
//  Stream() 在工作线程执行，回调也在工作线程触发；
//  UI 侧（App）负责用 PostMessage 转回主线程。Prepare 失败与缓存命中时回调在调用线程同步触发。
//
#pragma once
#include "Language.h"
#include "ProfileStatus.h"
#include "TranslationExecution.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct SettingsSnapshot;

/// Input of the BuiltIn model runtime (builtin::Stream).
struct TranslationRequest {
    std::wstring text;
    std::optional<Language> source;  // nullopt = 让模型自动判断
    Language target = Language::English;
    RewriteStyle style = RewriteStyle::Faithful;
};

namespace translator {

struct HealthCheckResult {
    bool ok = false;
    std::wstring message;
    int firstTokenMs = -1;
    int totalMs = 0;
};

/// Preset prompts (与 macOS TranslationPrompt 保持一致); forward to execution::PresetSystemPrompt /
/// PresetPlainPrompt, the single source of the preset wording.
std::string SystemPrompt(Language target, std::optional<Language> source, RewriteStyle style);
std::string PlainPrompt(const std::wstring& text, Language target, std::optional<Language> source, RewriteStyle style);

using DeltaFn = std::function<void(uint64_t reqId, const std::wstring& delta)>;
using DoneFn = std::function<void(uint64_t reqId, bool ok, const std::wstring& error)>;

/// 取消某个请求（对应流式任务尽快退出，不再回调）
void Cancel(uint64_t reqId);

/// 取消全部活跃请求（退出前调用）
void CancelAll();

// ---------- profile execution ----------

/// Connection values of a committed settings snapshot (the key stays in memory).
execution::ConnectionSnapshot ConnectionFrom(const SettingsSnapshot& snapshot);

/// Main thread only: freezes the committed profile bound to `entry` with `action`.
/// nullptr when the entry has no resolvable binding.
execution::ContextPtr FreezeEntry(ProfileEntry entry, ShortcutAction action);

/// Prepares the frozen context on the calling (main) thread and streams on a worker. Prepare
/// failures and cache hits call back synchronously before this returns. Deltas are the raw
/// provider stream; the cache stores execution::DeliveredOutput.
uint64_t Stream(const execution::ContextPtr& context, const std::wstring& text,
                std::optional<Language> targetOverride, DeltaFn onDelta, DoneFn onDone);

/// Stream() for tests (the profile editor's fixed-text test): never reads or writes the cache
/// and records no translation diagnostics, so a result always comes from the provider.
uint64_t StreamUncached(const execution::ContextPtr& context, const std::wstring& text,
                        std::optional<Language> targetOverride, DeltaFn onDelta, DoneFn onDone);

/// Blocking variant returning execution::DeliveredOutput; nullopt + error on failure.
std::optional<std::wstring> TranslateFully(const execution::ContextPtr& context, const std::wstring& text,
                                           std::wstring* error);

/// Async connection test of one explicit backend + model on `connection`, using fixed synthetic
/// text (never a selection, the clipboard or a profile prompt) and never the cache. Completion
/// runs on a worker thread, or synchronously when the request cannot be prepared.
void CheckConnectionAsync(const profilestatus::Probe& probe, const execution::ConnectionSnapshot& connection,
                          std::function<void(HealthCheckResult)> completion);

/// 测试钩子：清空缓存
void ClearCacheForTesting();

/// Pure streaming helpers shared by production parsing and contract self-tests.
std::optional<std::wstring> ParseOpenAISSELine(const std::string& line);
std::vector<std::string> AssembleLinesForTesting(const std::vector<std::string>& fragments);

} // namespace translator
