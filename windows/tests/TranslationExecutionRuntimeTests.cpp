#include "TranslationExecutionSupport.h"
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

// Runtime-facing pure contracts: physical-entry binding, delivered output and the
// fixed Prepare failure messages. Expected values come from the approved rules.
namespace execution_test {
namespace {

using execution::ContextPtr;
using execution::DeliveredOutput;
using execution::FreezeBound;
using execution::PrepareErrorMessage;
using execution::PrepareStatus;

TranslationProfile Named(const char* id, TranslationBackend backend, TranslationDirection direction) {
    TranslationProfile profile = MakeProfile(backend);
    profile.id = id;
    profile.name = id;
    profile.direction = direction;
    return profile;
}

ProfileConfiguration ThreeProfiles() {
    ProfileConfiguration configuration;
    configuration.profiles = {Named("p-read", TranslationBackend::Ollama, TranslationDirection::Auto),
                              Named("p-write", TranslationBackend::OpenAI, TranslationDirection::NativeToForeign),
                              Named("p-select", TranslationBackend::Ollama, TranslationDirection::ForeignToNative)};
    configuration.bindings = {"p-read", "p-write", "p-select"};
    return configuration;
}

void TestEntryBinding() {
    const ProfileConfiguration configuration = ThreeProfiles();
    const ContextPtr first =
        FreezeBound(configuration, ProfileEntry::Shortcut1, ShortcutAction::SmartPopup, MakeConnection());
    Check(first && first->profile.id == "p-read" && first->action == ShortcutAction::SmartPopup,
          "shortcut 1 runs the profile bound to slot 1 with its own action");
    const ContextPtr second =
        FreezeBound(configuration, ProfileEntry::Shortcut2, ShortcutAction::SmartReplace, MakeConnection());
    Check(second && second->profile.id == "p-write" && second->action == ShortcutAction::SmartReplace,
          "shortcut 2 runs the profile bound to slot 2 with its own action");
    const ContextPtr selection =
        FreezeBound(configuration, ProfileEntry::Selection, ShortcutAction::SmartPopup, MakeConnection());
    Check(selection && selection->profile.id == "p-select", "selection runs its bound profile");
    Check(first && first->connection.connectionEpoch == 7 && first->connection.openAIKey == kFakeKey,
          "a frozen entry carries the connection snapshot");
}

void TestSwappedAndSharedBindings() {
    ProfileConfiguration configuration = ThreeProfiles();
    std::swap(configuration.bindings.shortcut1ProfileId, configuration.bindings.shortcut2ProfileId);
    const ContextPtr first =
        FreezeBound(configuration, ProfileEntry::Shortcut1, ShortcutAction::SmartPopup, MakeConnection());
    Check(first && first->profile.id == "p-write", "swapped bindings follow the physical slot");
    configuration.bindings.shortcut2ProfileId = configuration.bindings.shortcut1ProfileId;
    const ContextPtr second =
        FreezeBound(configuration, ProfileEntry::Shortcut2, ShortcutAction::ForeignReplace, MakeConnection());
    Check(second && second->profile.id == "p-write" && second->action == ShortcutAction::ForeignReplace,
          "two slots may share one profile and keep their own actions");
}

void TestActionNotSlotDecidesDirection() {
    ProfileConfiguration configuration = ThreeProfiles();
    configuration.bindings.shortcut2ProfileId = "p-read";
    const ContextPtr popup =
        FreezeBound(configuration, ProfileEntry::Shortcut2, ShortcutAction::SmartPopup, MakeConnection());
    const ContextPtr replace =
        FreezeBound(configuration, ProfileEntry::Shortcut1, ShortcutAction::SmartReplace, MakeConnection());
    if (!Check(popup && replace, "both slots freeze the shared profile")) return;
    Check(execution::ResolveActionDirection(popup->profile, popup->action, kUndetectable).target ==
              Language::Chinese,
          "a popup action defaults an unknown source to native, whichever slot sends it");
    Check(execution::ResolveActionDirection(replace->profile, replace->action, kUndetectable).target ==
              Language::English,
          "a replace action defaults an unknown source to foreign, whichever slot sends it");
}

void TestUnresolvedBinding() {
    ProfileConfiguration configuration = ThreeProfiles();
    configuration.bindings.selectionProfileId = "missing-profile";
    Check(!FreezeBound(configuration, ProfileEntry::Selection, ShortcutAction::SmartPopup, MakeConnection()),
          "a dangling binding never falls back to another profile");
    Check(!FreezeBound(ProfileConfiguration{}, ProfileEntry::Shortcut1, ShortcutAction::SmartPopup,
                       MakeConnection()),
          "an empty configuration freezes nothing");
    const ContextPtr bound =
        FreezeBound(configuration, ProfileEntry::Shortcut1, ShortcutAction::SmartPopup, MakeConnection());
    Check(bound != nullptr, "other entries still resolve next to a dangling one");
}

void TestFrozenCopy() {
    ProfileConfiguration configuration = ThreeProfiles();
    const ContextPtr context =
        FreezeBound(configuration, ProfileEntry::Shortcut1, ShortcutAction::SmartPopup, MakeConnection());
    configuration.profiles[0].model = "edited-later";
    configuration.bindings.shortcut1ProfileId = "p-write";
    Check(context && context->profile.id == "p-read" && context->profile.model == "synthetic-ollama-model",
          "later configuration edits never reach a frozen entry");
}

void TestDeliveredOutput() {
    const TranslationProfile preset = MakeProfile(TranslationBackend::OpenAI);
    TranslationProfile custom = preset;
    custom.promptMode = PromptMode::Custom;
    custom.systemPrompt = "Translate.";
    const std::wstring raw = L"  line one\n\tline two\n";
    const std::optional<std::wstring> trimmed = DeliveredOutput(preset, raw);
    Check(trimmed && *trimmed == L"line one\n\tline two", "preset output keeps the legacy trim");
    const std::optional<std::wstring> exact = DeliveredOutput(custom, raw);
    Check(exact && *exact == raw, "custom output is delivered exactly (indentation, tabs, trailing newline)");
    Check(DeliveredOutput(preset, L"ok") == std::optional<std::wstring>(L"ok") &&
              DeliveredOutput(custom, L"ok") == std::optional<std::wstring>(L"ok"),
          "plain output passes through either mode");
    for (const wchar_t* blank : {L"", L"  ", L"\r\n\t \n"}) {
        Check(!DeliveredOutput(preset, blank), "blank preset output is a failure");
        Check(!DeliveredOutput(custom, blank), "blank custom output is a failure");
    }
}

void TestReplacementText() {
    using execution::ReplacementText;
    Check(ReplacementText(L"  line one\n\tline two\r\n\n", L"selected") == L"  line one\n\tline two",
          "Replace drops trailing line breaks but keeps indentation and inner line breaks");
    Check(ReplacementText(L"  line one\n", L"selected line\n") == L"  line one\n" &&
              ReplacementText(L"text\r\n", L"selected\r\n") == L"text\r\n",
          "a selection that ended with a line break keeps the output exactly");
    Check(ReplacementText(L"text  ", L"selected") == L"text  " && ReplacementText(L"text", L"") == L"text",
          "trailing spaces stay; only line breaks are dropped");
}

void TestPrepareMessages() {
    const PrepareStatus failures[] = {PrepareStatus::NotReady,        PrepareStatus::CapabilityRejected,
                                      PrepareStatus::TemplateInvalid, PrepareStatus::EndpointMissing,
                                      PrepareStatus::ModelMissing,    PrepareStatus::EndpointInvalid,
                                      PrepareStatus::EndpointInsecure, PrepareStatus::KeyMissing,
                                      PrepareStatus::KeyInvalid};
    const TranslationBackend backends[] = {TranslationBackend::BuiltIn, TranslationBackend::Ollama,
                                           TranslationBackend::OpenAI};
    for (TranslationBackend backend : backends)
        for (PrepareStatus status : failures)
            Check(!PrepareErrorMessage(status, backend).empty(), "every failed Prepare has a user-facing message");
    Check(PrepareErrorMessage(PrepareStatus::EndpointInsecure, TranslationBackend::OpenAI) !=
              PrepareErrorMessage(PrepareStatus::KeyMissing, TranslationBackend::OpenAI),
          "an insecure endpoint and a missing key read differently");
    Check(PrepareErrorMessage(PrepareStatus::EndpointMissing, TranslationBackend::Ollama) !=
              PrepareErrorMessage(PrepareStatus::EndpointMissing, TranslationBackend::OpenAI),
          "a missing endpoint names the provider it belongs to");
}

} // namespace

void RunRuntimeContractTests() {
    TestEntryBinding();
    TestSwappedAndSharedBindings();
    TestReplacementText();
    TestActionNotSlotDecidesDirection();
    TestUnresolvedBinding();
    TestFrozenCopy();
    TestDeliveredOutput();
    TestPrepareMessages();
}

} // namespace execution_test
