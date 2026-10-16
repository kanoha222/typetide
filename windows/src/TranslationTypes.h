// Shared translation identifiers; deliberately independent of Settings/Win32.
#pragma once
#include <optional>
#include <string>

enum class TranslationBackend { BuiltIn, Ollama, OpenAI };

enum class ShortcutAction {
    SmartPopup,
    NativePopup,
    ForeignPopup,
    SmartReplace,
    NativeReplace,
    ForeignReplace,
};

enum class PromptMode { Preset, Custom };
enum class ProfileEntry { Shortcut1, Shortcut2, Selection };

const char* BackendCode(TranslationBackend backend);
std::optional<TranslationBackend> BackendFromCode(const std::string& code);
const char* ShortcutActionCode(ShortcutAction action);
std::optional<ShortcutAction> ShortcutActionFromCode(const std::string& code);
bool IsReplaceAction(ShortcutAction action);
