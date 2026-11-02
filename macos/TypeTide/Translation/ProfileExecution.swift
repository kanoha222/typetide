//
//  ProfileExecution.swift
//  TypeTide
//
//  How a translation profile runs: direction, custom prompt rendering, built-in capability,
//  the delivered output and the text a Replace pastes. Pure functions (same rules as Windows).
//

import Foundation

enum ProfileExecution {
    /// The action decides native/foreign/smart; the profile supplies the languages and direction.
    static func route(for profile: TranslationProfile, action: ShortcutAction, text: String) -> TranslationRoute {
        TranslationRoute(source: nil, target: profile.foreignLanguage)  // RED scaffold
    }

    /// Replaces `{{source_language}}` and `{{target_language}}` (exact, case-sensitive) in one pass;
    /// anything else stays literal and substituted values are never scanned again.
    static func renderCustomPrompt(_ template: String, source: Language?, target: Language) -> String {
        template  // RED scaffold
    }

    /// The rendered system prompt of a custom profile; nil for presets.
    static func systemPrompt(for profile: TranslationProfile, route: TranslationRoute) -> String? {
        nil  // RED scaffold
    }

    /// Why this profile cannot run on its backend, if it cannot.
    static func capabilityError(for profile: TranslationProfile) -> String? {
        nil  // RED scaffold
    }

    /// Custom output is delivered exactly; preset output is trimmed. nil when blank.
    static func deliveredOutput(for profile: TranslationProfile, raw: String) -> String? {
        raw  // RED scaffold
    }

    /// The text a Replace pastes: no trailing line breaks unless the replaced text ended with one.
    static func replacementText(_ delivered: String, source: String) -> String {
        delivered  // RED scaffold
    }

    /// Ollama on another computer over plain HTTP: Replace actions always preview first.
    static func requiresPreview(_ profile: TranslationProfile, ollamaHost: String) -> Bool {
        false  // RED scaffold
    }
}
