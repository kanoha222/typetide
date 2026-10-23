#include "TranslationExecution.h"
#include <utility>

namespace execution {

// RED scaffold: compile shell only. Freeze is a trivial value constructor; every other
// helper intentionally returns its explicit not-ready value so the positive contract tests
// fail by assertion. The real helpers are added only after the behavioral RED is observed.

ContextPtr Freeze(TranslationProfile profile, ShortcutAction action, ConnectionSnapshot connection) {
    return std::make_shared<ExecutionContext>(
        ExecutionContext{std::move(profile), action, std::move(connection)});
}

ExecutionDirection ResolveActionDirection(const TranslationProfile& /*profile*/,
                                          ShortcutAction /*action*/, const std::wstring& /*text*/,
                                          std::optional<Language> /*targetOverride*/) {
    return {};
}

std::string PresetSystemPrompt(Language /*target*/, std::optional<Language> /*source*/,
                               RewriteStyle /*style*/) {
    return {};
}

std::string PresetPlainPrompt(const std::wstring& /*text*/, Language /*target*/,
                              std::optional<Language> /*source*/, RewriteStyle /*style*/) {
    return {};
}

RenderedPrompt RenderCustomPrompt(const std::string& /*templateText*/,
                                  std::optional<Language> /*source*/, Language /*target*/) {
    return {};
}

PreparedRequest Preview(const ExecutionContext& /*context*/, const std::wstring& /*text*/,
                        std::optional<Language> /*targetOverride*/) {
    return {};
}

PreparedRequest Prepare(const ExecutionContext& /*context*/, const std::wstring& /*text*/,
                        std::optional<Language> /*targetOverride*/) {
    return {};
}

} // namespace execution
