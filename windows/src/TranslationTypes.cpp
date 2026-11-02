#include "TranslationTypes.h"

const char* BackendCode(TranslationBackend backend) {
    switch (backend) {
    case TranslationBackend::BuiltIn:
        return "builtin";
    case TranslationBackend::Ollama:
        return "ollama";
    case TranslationBackend::OpenAI:
        return "openai";
    }
    return "builtin";
}

std::optional<TranslationBackend> BackendFromCode(const std::string& code) {
    if (code == "builtin") return TranslationBackend::BuiltIn;
    if (code == "ollama") return TranslationBackend::Ollama;
    if (code == "openai") return TranslationBackend::OpenAI;
    return std::nullopt;
}

const char* ShortcutActionCode(ShortcutAction action) {
    switch (action) {
    case ShortcutAction::SmartPopup:
        return "smartPopup";
    case ShortcutAction::NativePopup:
        return "nativePopup";
    case ShortcutAction::ForeignPopup:
        return "foreignPopup";
    case ShortcutAction::SmartReplace:
        return "smartReplace";
    case ShortcutAction::NativeReplace:
        return "nativeReplace";
    case ShortcutAction::ForeignReplace:
        return "foreignReplace";
    }
    return "smartPopup";
}

std::optional<ShortcutAction> ShortcutActionFromCode(const std::string& code) {
    if (code == "smartPopup") return ShortcutAction::SmartPopup;
    if (code == "nativePopup") return ShortcutAction::NativePopup;
    if (code == "foreignPopup") return ShortcutAction::ForeignPopup;
    if (code == "smartReplace") return ShortcutAction::SmartReplace;
    if (code == "nativeReplace") return ShortcutAction::NativeReplace;
    if (code == "foreignReplace") return ShortcutAction::ForeignReplace;
    return std::nullopt;
}

bool IsReplaceAction(ShortcutAction action) {
    switch (action) {
    case ShortcutAction::SmartReplace:
    case ShortcutAction::NativeReplace:
    case ShortcutAction::ForeignReplace:
        return true;
    case ShortcutAction::SmartPopup:
    case ShortcutAction::NativePopup:
    case ShortcutAction::ForeignPopup:
        return false;
    }
    return false;
}
