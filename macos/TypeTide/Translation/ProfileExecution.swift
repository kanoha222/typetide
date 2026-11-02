//
//  ProfileExecution.swift
//  TypeTide
//
//  How a translation profile runs: direction, custom prompt rendering, built-in capability,
//  the delivered output and the text a Replace pastes. Pure functions (same rules as Windows).
//

import Foundation

enum ProfileExecution {
    static let sourceToken = "{{source_language}}"
    static let targetToken = "{{target_language}}"
    /// Fixed synthetic text for profile and connection tests: never a selection or the clipboard.
    static let testText = "Hello! This is a TypeTide profile test."

    /// The action decides native/foreign/smart; the profile supplies the languages and direction.
    static func route(for profile: TranslationProfile, action: ShortcutAction, text: String) -> TranslationRoute {
        switch action {
        case .nativePopup, .nativeReplace:
            return TranslationRoute(source: Language.detect(in: text), target: profile.nativeLanguage)
        case .foreignPopup, .foreignReplace:
            return TranslationRoute(source: Language.detect(in: text), target: profile.foreignLanguage)
        case .smartPopup, .smartReplace:
            return TranslationRouting.resolve(text: text, mode: profile.direction, isWrite: !action.usesPopup,
                                              native: profile.nativeLanguage, foreign: profile.foreignLanguage)
        }
    }

    /// Replaces `{{source_language}}` and `{{target_language}}` (exact, case-sensitive) in one pass;
    /// anything else stays literal and substituted values are never scanned again.
    static func renderCustomPrompt(_ template: String, source: Language?, target: Language) -> String {
        let sourceName = source?.promptName ?? "auto-detected language"
        var result = ""
        var index = template.startIndex
        while index < template.endIndex {
            let remainder = template[index...]
            if remainder.hasPrefix(sourceToken) {
                result += sourceName
                index = template.index(index, offsetBy: sourceToken.count)
            } else if remainder.hasPrefix(targetToken) {
                result += target.promptName
                index = template.index(index, offsetBy: targetToken.count)
            } else {
                result.append(template[index])
                index = template.index(after: index)
            }
        }
        return result
    }

    /// The rendered system prompt of a custom profile; nil for presets.
    static func systemPrompt(for profile: TranslationProfile, route: TranslationRoute) -> String? {
        guard profile.promptMode == .custom else { return nil }
        return renderCustomPrompt(profile.systemPrompt, source: route.source, target: route.target)
    }

    /// Why this profile cannot run on its backend, if it cannot.
    static func capabilityError(for profile: TranslationProfile) -> String? {
        guard profile.backend == .builtIn else { return nil }
        if profile.promptMode == .custom {
            return "The built-in model can't run a custom prompt. Choose Ollama or an OpenAI-compatible API for this profile."
        }
        if profile.presetStyle != .faithful {
            return "The built-in model supports Faithful translation only. Choose Faithful, or use Ollama/API for other styles."
        }
        return nil
    }

    /// Custom output is delivered exactly; preset output is trimmed. nil when blank.
    static func deliveredOutput(for profile: TranslationProfile, raw: String) -> String? {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        return profile.promptMode == .custom ? raw : trimmed
    }

    /// The text a Replace pastes: no trailing line breaks unless the replaced text ended with one,
    /// so a pasted line is never also "entered" (a terminal would run it).
    static func replacementText(_ delivered: String, source: String) -> String {
        if let last = source.last, last.isNewline { return delivered }
        var text = delivered
        while let last = text.last, last.isNewline { text.removeLast() }
        return text
    }

    /// Ollama on another computer over plain HTTP: Replace actions always preview first.
    static func requiresPreview(_ profile: TranslationProfile, ollamaHost: String) -> Bool {
        guard profile.backend == .ollama, let url = URL(string: ollamaHost),
              url.scheme?.lowercased() == "http" else { return false }
        let host = url.host?.lowercased() ?? ""
        return !(host == "localhost" || host == "127.0.0.1" || host == "::1")
    }

    /// Same scheme, host and port: an API key belongs to one origin.
    static func sameOrigin(_ a: String, _ b: String) -> Bool {
        guard let left = URLComponents(string: a.trimmingCharacters(in: .whitespacesAndNewlines)),
              let right = URLComponents(string: b.trimmingCharacters(in: .whitespacesAndNewlines)),
              let scheme = left.scheme?.lowercased(), scheme == right.scheme?.lowercased(),
              scheme == "http" || scheme == "https",
              left.user == nil, right.user == nil,
              let host = left.host?.lowercased(), !host.isEmpty, host == right.host?.lowercased() else { return false }
        let defaultPort = scheme == "https" ? 443 : 80
        return (left.port ?? defaultPort) == (right.port ?? defaultPort)
    }
}
