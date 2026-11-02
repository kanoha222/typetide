//
//  TranslationProfile.swift
//  TypeTide
//
//  Named translation profiles, with the same semantics as the Windows app: languages, direction,
//  a preset style or a custom system prompt that replaces the default instructions entirely, and
//  the backend + model, bound to Shortcut 1, Shortcut 2 and the selection trigger. Value types
//  only; ProfileStore persists them.
//

import Foundation

extension Language: Codable {}
extension TranslationDirection: Codable {}
extension RewriteStyle: Codable {}
extension TranslationBackend: Codable {}

enum PromptMode: String, Codable, CaseIterable, Identifiable {
    case preset
    case custom

    var id: String { rawValue }
}

/// The three physical entries a profile can be bound to.
enum ProfileEntry: String, CaseIterable, Identifiable {
    case shortcut1
    case shortcut2
    case selection

    var id: String { rawValue }
    var label: String {
        switch self {
        case .shortcut1: return "Shortcut 1"
        case .shortcut2: return "Shortcut 2"
        case .selection: return "Selection"
        }
    }
}

struct TranslationProfile: Codable, Equatable, Identifiable {
    var id: String
    var name: String
    var nativeLanguage: Language
    var foreignLanguage: Language
    var direction: TranslationDirection
    var promptMode: PromptMode
    var presetStyle: RewriteStyle
    /// Kept while the preset mode is selected, so switching back restores it.
    var systemPrompt: String
    var backend: TranslationBackend
    /// Empty for the built-in model; explicit for Ollama and OpenAI-compatible APIs.
    var model: String
}

struct ProfileBindings: Codable, Equatable {
    var shortcut1ProfileId: String
    var shortcut2ProfileId: String
    var selectionProfileId: String

    func profileId(for entry: ProfileEntry) -> String {
        switch entry {
        case .shortcut1: return shortcut1ProfileId
        case .shortcut2: return shortcut2ProfileId
        case .selection: return selectionProfileId
        }
    }
}

struct ProfileConfiguration: Codable, Equatable {
    var profiles: [TranslationProfile]
    var bindings: ProfileBindings

    static let maxProfiles = 64
    static let maxNameUTF16 = 80
    static let maxPromptUTF16 = 32 * 1024
    static let maxModelBytes = 256
    static let maxIdBytes = 128

    static var defaults: ProfileConfiguration {
        let read = TranslationProfile(id: "default-read", name: "默认翻译", nativeLanguage: .chinese,
                                      foreignLanguage: .english, direction: .auto, promptMode: .preset,
                                      presetStyle: .faithful, systemPrompt: "", backend: .builtIn, model: "")
        var rewrite = read
        rewrite.id = "default-rewrite"
        rewrite.name = "默认改写"
        rewrite.direction = .nativeToForeign
        return ProfileConfiguration(profiles: [read, rewrite],
                                    bindings: ProfileBindings(shortcut1ProfileId: read.id,
                                                              shortcut2ProfileId: rewrite.id,
                                                              selectionProfileId: read.id))
    }

    /// The profile bound to `entry`, or nil when the binding does not resolve.
    func profile(for entry: ProfileEntry) -> TranslationProfile? {
        let id = bindings.profileId(for: entry)
        return profiles.first { $0.id == id }
    }

    /// A copy with only `entry` bound to `profileId` (not validated).
    func withBinding(_ entry: ProfileEntry, to profileId: String) -> ProfileConfiguration {
        self  // RED scaffold
    }

    /// nil when the configuration may be saved, otherwise the reason it may not.
    func validationError() -> String? {
        nil  // RED scaffold
    }

    /// Profiles generated from the settings of builds without profiles: each shortcut is bound by
    /// its saved action (popup → the read profile, replace → the rewrite profile), selection reads.
    static func migrated(from legacy: LegacyTranslationSettings) -> ProfileConfiguration {
        defaults  // RED scaffold
    }
}

/// The translation settings of builds without profiles.
struct LegacyTranslationSettings: Equatable {
    var nativeLanguage: Language = .chinese
    var foreignLanguage: Language = .english
    var readDirection: TranslationDirection = .auto
    var rewriteDirection: TranslationDirection = .auto
    var rewriteStyle: RewriteStyle = .faithful
    var backend: TranslationBackend = .builtIn
    var openAIModel = "gpt-4o-mini"
    var ollamaModel = "qwen2.5:3b"
    var readAction: ShortcutAction = .smartPopup
    var rewriteAction: ShortcutAction = .smartReplace

    static func read(from defaults: UserDefaults) -> LegacyTranslationSettings {
        LegacyTranslationSettings()  // RED scaffold
    }
}
