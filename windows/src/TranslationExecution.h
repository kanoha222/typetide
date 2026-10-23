//
//  TranslationExecution.h — pure effective-request helpers for translation
//  profiles: direction, prompts, provider messages/bodies, endpoint policy and
//  cache identity. Deliberately independent of Settings, Credentials, Log, GUI
//  and the BuiltIn model runtime so it can be self-tested on its own.
//
//  Threading: an ExecutionContext is frozen on the main thread (Freeze copies
//  plain values, including the already-resolved API key held in memory only)
//  and may then be read from any thread. Nothing here reads Settings::shared().
//
#pragma once
#include "EndpointPolicy.h"
#include "Language.h"
#include "TranslationProfile.h"
#include "TranslationTypes.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace execution {

/// Bumped when adapter parameters or the retry policy change; part of the cache identity.
inline constexpr char kPolicyVersion[] = "1";

/// Source-language phrase substituted for {{source_language}} when detection failed.
inline constexpr char kUnknownSourceName[] = "auto-detected language";

/// The only warning code emitted for unknown {{...}} tokens. It never carries the token.
inline constexpr char kWarningUnknownPlaceholder[] = "unknown_placeholder";

/// Fixed local BuiltIn model pin, mirrored from builtin:: (BuiltInModel.cpp kModelName,
/// kModelURL commit and kSha256). The runtime phase decides how to share the original
/// without linking the model runtime into the pure target.
inline constexpr char kBuiltInModelRevision[] =
    "translategemma-4b-it.Q4_K_M.gguf@b89d835d2f2b802f50f3a30f1f963c7c43de4723"
    "#sha256=81200d03e843d2ec1ece6eeafe7d13cb6e5211e1fcd336ade55790b683a08330";

// ---------- frozen inputs ----------

/// Connection values captured on the main thread. Mirrors the same-named fields of
/// SettingsSnapshot; the adapter from SettingsSnapshot arrives with the runtime phase.
struct ConnectionSnapshot {
    uint64_t connectionEpoch = 0;
    std::string ollamaHost;
    int ollamaPort = 11434;
    std::string openAIBaseURL;
    std::string openAIKey;  // resolved secret: memory only, never in identity/body/logs
};

struct ExecutionContext {
    TranslationProfile profile;  // value copy, never a pointer into live settings
    ShortcutAction action = ShortcutAction::SmartPopup;
    ConnectionSnapshot connection;
};

using ContextPtr = std::shared_ptr<const ExecutionContext>;

/// Copies the values into an immutable context.
ContextPtr Freeze(TranslationProfile profile, ShortcutAction action, ConnectionSnapshot connection);

// ---------- direction ----------

struct ExecutionDirection {
    bool ready = false;                // false only for the unimplemented scaffold
    std::optional<Language> source;    // nullopt = detection failed / unknown
    Language target = Language::English;
};

/// Native/Foreign actions: target = the profile language, source = Detect(text) (a detected
/// third language is kept). Smart actions: lang::ResolveDirection with isWrite =
/// IsReplaceAction(action). targetOverride replaces ONLY the resolved target (retarget);
/// the source and the saved profile are untouched.
ExecutionDirection ResolveActionDirection(const TranslationProfile& profile, ShortcutAction action,
                                          const std::wstring& text,
                                          std::optional<Language> targetOverride = std::nullopt);

// ---------- prompts ----------

/// Byte-identical to the current translator::SystemPrompt / translator::PlainPrompt (which
/// stay in Translator.cpp until the runtime phase forwards to these).
std::string PresetSystemPrompt(Language target, std::optional<Language> source, RewriteStyle style);
std::string PresetPlainPrompt(const std::wstring& text, Language target,
                              std::optional<Language> source, RewriteStyle style);

struct RenderedPrompt {
    bool ok = false;                          // false: blank template (or scaffold)
    std::string text;                         // rendered system prompt, whitespace preserved
    std::vector<std::string> unknownTokens;   // e.g. "{{foo}}"; UI memory only, never logged
    std::vector<std::string> warnings;        // fixed codes only (kWarningUnknownPlaceholder)
};

/// Single left-to-right pass replacing exactly "{{source_language}}" and "{{target_language}}"
/// (case-sensitive). Anything else, including other "{{name}}" tokens and stray braces, stays
/// literal text. Appends no instruction; selected text is never an input here.
RenderedPrompt RenderCustomPrompt(const std::string& templateText, std::optional<Language> source,
                                  Language target);

// ---------- prepared request ----------

enum class PrepareStatus {
    NotReady,          // scaffold state: helper not implemented yet (NOT a user error)
    Ok,
    CapabilityRejected,  // e.g. BuiltIn with a custom prompt or a non-faithful style
    TemplateInvalid,     // blank custom prompt
    EndpointMissing,
    ModelMissing,
    EndpointInvalid,     // malformed/unsafe authority, bad port, query/fragment, Ollama host+port clash
    EndpointInsecure,    // OpenAI over remote HTTP
    KeyMissing,          // OpenAI over HTTPS without a key
};

struct PreparedRequest {
    PrepareStatus status = PrepareStatus::NotReady;
    TranslationBackend backend = TranslationBackend::BuiltIn;
    ExecutionDirection direction;
    std::wstring finalUrl;        // exact string for the transport; empty for BuiltIn / Preview
    std::string system;           // rendered system text (OpenAI system role / Ollama "system")
    std::string user;             // OpenAI user content / Ollama "prompt" / BuiltIn UTF-8 text
    std::string body;             // initial provider JSON; empty for BuiltIn
    std::string retryBody;        // Ollama only: body without "think"; otherwise empty
    std::string cacheIdentity;    // length-safe canonical identity; empty for Preview
    std::vector<std::string> unknownTokens;  // UI memory only
    std::vector<std::string> warnings;       // fixed codes only
    bool ok() const { return status == PrepareStatus::Ok; }
};

/// Template, direction, capability and message/body display. Needs no endpoint, API key,
/// model name or reachable service (works for half-filled UI drafts). It therefore must NOT
/// call profiles::Validate / ValidateCapability (they reject a blank remote model); the
/// BuiltIn capability rule (preset + faithful only) is checked directly. A blank custom
/// prompt (empty or Unicode-whitespace-only, as profiles::Validate defines it) is
/// TemplateInvalid.
PreparedRequest Preview(const ExecutionContext& context, const std::wstring& text,
                        std::optional<Language> targetOverride = std::nullopt);

/// Preview plus provider endpoint / model / authentication validation, finalUrl and
/// cacheIdentity. This is what the runtime consumes.
PreparedRequest Prepare(const ExecutionContext& context, const std::wstring& text,
                        std::optional<Language> targetOverride = std::nullopt);

} // namespace execution
