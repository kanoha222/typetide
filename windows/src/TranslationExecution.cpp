#include "TranslationExecution.h"
#include "Json.h"
#include "Util.h"
#include <cstddef>
#include <string>
#include <utility>

namespace execution {

namespace {

constexpr char kSourceToken[] = "{{source_language}}";
constexpr char kTargetToken[] = "{{target_language}}";

std::string StyleSuffix(RewriteStyle style) {
    const char* instruction = lang::StyleInstruction(style);
    return *instruction ? std::string(" ") + instruction : std::string();
}

bool StartsAt(const std::string& text, std::size_t pos, const char* token) {
    return text.compare(pos, std::char_traits<char>::length(token), token) == 0;
}

// Length of an unknown "{{name}}" token starting at pos (non-empty name without braces), or 0.
std::size_t UnknownTokenLength(const std::string& text, std::size_t pos) {
    const std::size_t close = text.find("}}", pos + 2);
    if (close == std::string::npos || close == pos + 2) return 0;
    if (text.find_first_of("{}", pos + 2) < close) return 0;
    return close + 2 - pos;
}

void NoteUnknownToken(std::string token, RenderedPrompt* result) {
    if (result->warnings.empty()) result->warnings.push_back(kWarningUnknownPlaceholder);
    for (const std::string& known : result->unknownTokens)
        if (known == token) return;
    result->unknownTokens.push_back(std::move(token));
}

// ---------- messages and provider bodies ----------

PrepareStatus FillMessages(const TranslationProfile& profile, const std::wstring& text,
                           PreparedRequest* request) {
    const ExecutionDirection& direction = request->direction;
    if (profile.promptMode == PromptMode::Custom) {
        RenderedPrompt rendered = RenderCustomPrompt(profile.systemPrompt, direction.source, direction.target);
        if (!rendered.ok) return PrepareStatus::TemplateInvalid;
        request->system = std::move(rendered.text);
        request->user = util::Narrow(text);
        request->unknownTokens = std::move(rendered.unknownTokens);
        request->warnings = std::move(rendered.warnings);
        return PrepareStatus::Ok;
    }
    switch (profile.backend) {
    case TranslationBackend::OpenAI:
        request->system = PresetSystemPrompt(direction.target, direction.source, profile.presetStyle);
        request->user = util::Narrow(text);
        break;
    case TranslationBackend::Ollama:
        request->user = PresetPlainPrompt(text, direction.target, direction.source, profile.presetStyle);
        break;
    case TranslationBackend::BuiltIn:
        request->user = util::Narrow(text);
        break;
    }
    return PrepareStatus::Ok;
}

json::Value Message(const char* role, const std::string& content) {
    json::Object message;
    message["role"] = role;
    message["content"] = content;
    return json::Value(std::move(message));
}

std::string OpenAIBody(const std::string& model, const PreparedRequest& request) {
    json::Array messages;
    messages.push_back(Message("system", request.system));
    messages.push_back(Message("user", request.user));
    json::Object body;
    body["model"] = model;
    body["stream"] = true;
    body["messages"] = json::Value(std::move(messages));
    return json::Value(std::move(body)).dump();
}

// Same fields and sampling options as the legacy runOllama body. A custom prompt adds
// "system"; the retry body (think rejected with HTTP 400) only drops "think".
std::string OllamaBody(const std::string& model, const PreparedRequest& request, bool disableThink) {
    json::Object options;
    options["temperature"] = 0.3;
    options["top_p"] = 0.9;
    options["top_k"] = 40;
    json::Object body;
    body["model"] = model;
    if (!request.system.empty()) body["system"] = request.system;
    body["prompt"] = request.user;
    body["stream"] = true;
    body["keep_alive"] = "10m";
    if (disableThink) body["think"] = false;
    body["options"] = json::Value(std::move(options));
    return json::Value(std::move(body)).dump();
}

bool BuiltInSupports(const TranslationProfile& profile) {
    return profile.promptMode == PromptMode::Preset && profile.presetStyle == RewriteStyle::Faithful;
}

// ---------- endpoints ----------

std::string TrimTrailingSlashes(std::string text) {
    while (!text.empty() && text.back() == '/') text.pop_back();
    return text;
}

// HTTP only for the three exact loopback hosts; HTTPS needs a key at runtime (Preview never
// asks for one). The base must not carry a query or fragment: "/chat/completions" is appended.
PrepareStatus OpenAIEndpoint(const ExecutionContext& context, std::wstring* finalUrl) {
    const std::string base = TrimTrailingSlashes(context.connection.openAIBaseURL);
    if (base.empty()) return PrepareStatus::EndpointMissing;
    if (profiles::IsBlankText(context.profile.model)) return PrepareStatus::ModelMissing;
    const EndpointInfo info = ClassifyEndpoint(util::Widen(base));
    const bool parsed = info.kind == EndpointKind::LoopbackHttp || info.kind == EndpointKind::Https ||
                        info.kind == EndpointKind::RemoteHttp;
    if (!parsed || info.hasQueryOrFragment) return PrepareStatus::EndpointInvalid;
    if (info.kind == EndpointKind::RemoteHttp) return PrepareStatus::EndpointInsecure;
    if (info.kind == EndpointKind::Https && context.connection.openAIKey.empty())
        return PrepareStatus::KeyMissing;
    *finalUrl = info.url + L"/chat/completions";
    return PrepareStatus::Ok;
}

// The configured host is scheme://authority only; the port comes from its own field, so an
// explicit port, path, query or fragment in the host is refused instead of being joined.
// Remote HTTP stays allowed for Ollama (it never carries an API key).
PrepareStatus OllamaEndpoint(const ExecutionContext& context, std::wstring* finalUrl) {
    const std::string host = TrimTrailingSlashes(context.connection.ollamaHost);
    if (host.empty()) return PrepareStatus::EndpointMissing;
    if (profiles::IsBlankText(context.profile.model)) return PrepareStatus::ModelMissing;
    const int port = context.connection.ollamaPort;
    if (port < 1 || port > 65535) return PrepareStatus::EndpointInvalid;
    const EndpointInfo info = ClassifyEndpoint(util::Widen(host));
    const bool usable = info.kind == EndpointKind::LoopbackHttp || info.kind == EndpointKind::Https ||
                        info.kind == EndpointKind::RemoteHttp;
    if (!usable || info.explicitPort || info.hasPath || info.hasQueryOrFragment)
        return PrepareStatus::EndpointInvalid;
    *finalUrl = info.url + L":" + std::to_wstring(port) + L"/api/generate";
    return PrepareStatus::Ok;
}

PrepareStatus ResolveEndpoint(const ExecutionContext& context, std::wstring* finalUrl) {
    switch (context.profile.backend) {
    case TranslationBackend::BuiltIn: return PrepareStatus::Ok;
    case TranslationBackend::Ollama: return OllamaEndpoint(context, finalUrl);
    case TranslationBackend::OpenAI: return OpenAIEndpoint(context, finalUrl);
    }
    return PrepareStatus::CapabilityRejected;
}

// ---------- cache identity ----------

void AppendField(std::string* identity, const char* name, const std::string& value) {
    *identity += name;
    *identity += '=';
    *identity += std::to_string(value.size());
    *identity += ':';
    *identity += value;
    *identity += '\n';
}

// Length-prefixed fields, so no prompt/text content can forge another identity. The provider
// body is a pure function of these fields under kPolicyVersion. Never included: the API key,
// the credential ref, the profile id and the display name. BuiltIn ignores the connection.
std::string CacheIdentity(const ExecutionContext& context, const PreparedRequest& request) {
    const TranslationProfile& profile = context.profile;
    const bool builtIn = profile.backend == TranslationBackend::BuiltIn;
    const ExecutionDirection& direction = request.direction;
    std::string identity;
    AppendField(&identity, "policy", kPolicyVersion);
    AppendField(&identity, "backend", BackendCode(profile.backend));
    AppendField(&identity, "endpoint", builtIn ? std::string() : util::Narrow(request.finalUrl));
    AppendField(&identity, "model", builtIn ? std::string(kBuiltInModelRevision) : profile.model);
    AppendField(&identity, "source", direction.source ? lang::Code(*direction.source) : "");
    AppendField(&identity, "target", lang::Code(direction.target));
    AppendField(&identity, "system", request.system);
    AppendField(&identity, "user", request.user);
    AppendField(&identity, "epoch", builtIn ? std::string() : std::to_string(context.connection.connectionEpoch));
    return identity;
}

} // namespace

ContextPtr Freeze(TranslationProfile profile, ShortcutAction action, ConnectionSnapshot connection) {
    return std::make_shared<ExecutionContext>(
        ExecutionContext{std::move(profile), action, std::move(connection)});
}

ExecutionDirection ResolveActionDirection(const TranslationProfile& profile, ShortcutAction action,
                                          const std::wstring& text,
                                          std::optional<Language> targetOverride) {
    ExecutionDirection direction;
    switch (action) {
    case ShortcutAction::NativePopup:
    case ShortcutAction::NativeReplace:
        direction.source = lang::Detect(text);
        direction.target = profile.nativeLanguage;
        break;
    case ShortcutAction::ForeignPopup:
    case ShortcutAction::ForeignReplace:
        direction.source = lang::Detect(text);
        direction.target = profile.foreignLanguage;
        break;
    default: {
        const ResolvedDirection resolved =
            lang::ResolveDirection(text, profile.direction, IsReplaceAction(action),
                                   profile.nativeLanguage, profile.foreignLanguage);
        direction.source = resolved.source;
        direction.target = resolved.target;
        break;
    }
    }
    if (targetOverride) direction.target = *targetOverride;
    direction.ready = true;
    return direction;
}

std::string PresetSystemPrompt(Language target, std::optional<Language> source, RewriteStyle style) {
    std::string prompt = "You are a professional translation engine. Translate the user's text from ";
    prompt += source ? lang::PromptName(*source) : "the detected language";
    prompt += " into ";
    prompt += lang::PromptName(target);
    prompt += ".";
    prompt += StyleSuffix(style);
    prompt += " Output ONLY the translation, with no quotes, no explanations, no extra notes. "
              "Preserve the original meaning and formatting.";
    return prompt;
}

std::string PresetPlainPrompt(const std::wstring& text, Language target,
                              std::optional<Language> source, RewriteStyle style) {
    std::string prompt = "Translate the following text from ";
    prompt += source ? lang::PromptName(*source) : "the source language";
    prompt += " to ";
    prompt += lang::PromptName(target);
    prompt += ".";
    prompt += StyleSuffix(style);
    prompt += " Only output the translation, no explanation.\n\n";
    prompt += util::Narrow(text);
    return prompt;
}

RenderedPrompt RenderCustomPrompt(const std::string& templateText, std::optional<Language> source,
                                  Language target) {
    RenderedPrompt result;
    if (profiles::IsBlankText(templateText)) return result;
    const std::string sourceName = source ? lang::PromptName(*source) : kUnknownSourceName;
    const std::string targetName = lang::PromptName(target);
    std::size_t pos = 0;
    while (pos < templateText.size()) {
        if (!StartsAt(templateText, pos, "{{")) {
            result.text += templateText[pos++];
        } else if (StartsAt(templateText, pos, kSourceToken)) {
            result.text += sourceName;
            pos += sizeof(kSourceToken) - 1;
        } else if (StartsAt(templateText, pos, kTargetToken)) {
            result.text += targetName;
            pos += sizeof(kTargetToken) - 1;
        } else if (const std::size_t length = UnknownTokenLength(templateText, pos)) {
            NoteUnknownToken(templateText.substr(pos, length), &result);
            result.text.append(templateText, pos, length);
            pos += length;
        } else {
            result.text += templateText[pos++];
        }
    }
    result.ok = true;
    return result;
}

PreparedRequest Preview(const ExecutionContext& context, const std::wstring& text,
                        std::optional<Language> targetOverride) {
    const TranslationProfile& profile = context.profile;
    PreparedRequest request;
    request.backend = profile.backend;
    if (profile.backend == TranslationBackend::BuiltIn && !BuiltInSupports(profile)) {
        request.status = PrepareStatus::CapabilityRejected;
        return request;
    }
    request.direction = ResolveActionDirection(profile, context.action, text, targetOverride);
    request.status = FillMessages(profile, text, &request);
    if (!request.ok()) return request;
    if (profile.backend == TranslationBackend::OpenAI) {
        request.body = OpenAIBody(profile.model, request);
    } else if (profile.backend == TranslationBackend::Ollama) {
        request.body = OllamaBody(profile.model, request, true);
        request.retryBody = OllamaBody(profile.model, request, false);
    }
    return request;
}

PreparedRequest Prepare(const ExecutionContext& context, const std::wstring& text,
                        std::optional<Language> targetOverride) {
    PreparedRequest request = Preview(context, text, targetOverride);
    if (!request.ok()) return request;
    request.status = ResolveEndpoint(context, &request.finalUrl);
    if (!request.ok()) {
        request.finalUrl.clear();
        return request;
    }
    request.cacheIdentity = CacheIdentity(context, request);
    return request;
}

ContextPtr FreezeBound(const ProfileConfiguration& configuration, ProfileEntry entry,
                       ShortcutAction action, ConnectionSnapshot connection) {
    const TranslationProfile* profile = profiles::BoundProfile(configuration, entry);
    if (!profile) return nullptr;
    return Freeze(*profile, action, std::move(connection));
}

// Trim only decides "blank"; a custom prompt owns its output formatting exactly.
std::optional<std::wstring> DeliveredOutput(const TranslationProfile& profile, const std::wstring& raw) {
    std::wstring trimmed = util::Trim(raw);
    if (trimmed.empty()) return std::nullopt;
    return profile.promptMode == PromptMode::Custom ? raw : trimmed;
}

std::wstring PrepareErrorMessage(PrepareStatus status, TranslationBackend backend) {
    const bool ollama = backend == TranslationBackend::Ollama;
    switch (status) {
    case PrepareStatus::Ok:
        return {};
    case PrepareStatus::CapabilityRejected:
        return L"Built-in translation supports the Faithful preset only. Choose Ollama or API for "
               L"custom prompts or other styles.";
    case PrepareStatus::TemplateInvalid:
        return L"The custom prompt is empty.";
    case PrepareStatus::EndpointMissing:
        return ollama ? L"Missing Ollama host" : L"Missing cloud API base URL";
    case PrepareStatus::ModelMissing:
        return ollama ? L"Missing Ollama model name for this profile" : L"Missing cloud model name for this profile";
    case PrepareStatus::EndpointInvalid:
        return ollama ? L"Invalid Ollama host. Use http(s)://host only and set the port separately."
                      : L"Invalid cloud API base URL. Remove any credentials, query or fragment.";
    case PrepareStatus::EndpointInsecure:
        return L"Refusing plain HTTP to a remote API endpoint. Use HTTPS or a local endpoint.";
    case PrepareStatus::KeyMissing:
        return L"Missing cloud API key";
    case PrepareStatus::NotReady:
        break;
    }
    return L"Translation could not be prepared.";
}

} // namespace execution
