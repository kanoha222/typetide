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
        var copy = self
        switch entry {
        case .shortcut1: copy.bindings.shortcut1ProfileId = profileId
        case .shortcut2: copy.bindings.shortcut2ProfileId = profileId
        case .selection: copy.bindings.selectionProfileId = profileId
        }
        return copy
    }

    /// nil when the configuration may be saved, otherwise the reason it may not.
    func validationError() -> String? {
        guard !profiles.isEmpty else { return "At least one translation profile is required." }
        guard profiles.count <= Self.maxProfiles else {
            return "TypeTide supports up to \(Self.maxProfiles) translation profiles."
        }
        var seen = Set<String>()
        for item in profiles {
            if let error = Self.validationError(of: item) { return error }
            guard seen.insert(item.id).inserted else { return "Two profiles share the same identifier." }
        }
        for entry in ProfileEntry.allCases where profile(for: entry) == nil {
            return "\(entry.label) is bound to a profile that does not exist."
        }
        return nil
    }

    /// The reason `profile` may not be saved, or nil. Text is never truncated to fit.
    static func validationError(of profile: TranslationProfile) -> String? {
        if profile.id.isEmpty || profile.id.utf8.count > maxIdBytes { return "A profile has an invalid identifier." }
        if isBlank(profile.name) { return "Every profile needs a name." }
        if profile.name.utf16.count > maxNameUTF16 {
            return "The profile name “\(profile.name.prefix(24))…” is longer than \(maxNameUTF16) characters."
        }
        if profile.systemPrompt.utf16.count > maxPromptUTF16 {
            return "The system prompt of “\(profile.name)” is longer than \(maxPromptUTF16) characters."
        }
        if profile.promptMode == .custom && isBlank(profile.systemPrompt) {
            return "The custom system prompt of “\(profile.name)” is empty."
        }
        if profile.model.utf8.count > maxModelBytes { return "The model name of “\(profile.name)” is too long." }
        if profile.backend != .builtIn && isBlank(profile.model) {
            return "“\(profile.name)” needs a model name for \(profile.backend.displayName)."
        }
        return nil
    }

    static func isBlank(_ text: String) -> Bool {
        text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    /// Profiles generated from the settings of builds without profiles: each shortcut is bound by
    /// its saved action (popup → the read profile, replace → the rewrite profile), selection reads.
    static func migrated(from legacy: LegacyTranslationSettings) -> ProfileConfiguration {
        let fallback = LegacyTranslationSettings()
        func usable(_ model: String, orUse replacement: String) -> String {
            isBlank(model) || model.utf8.count > maxModelBytes ? replacement : model
        }
        let model: String
        switch legacy.backend {
        case .builtIn: model = ""
        case .openai: model = usable(legacy.openAIModel, orUse: fallback.openAIModel)
        case .ollama: model = usable(legacy.ollamaModel, orUse: fallback.ollamaModel)
        }
        let read = TranslationProfile(id: "legacy-read", name: "默认翻译", nativeLanguage: legacy.nativeLanguage,
                                      foreignLanguage: legacy.foreignLanguage, direction: legacy.readDirection,
                                      promptMode: .preset, presetStyle: .faithful, systemPrompt: "",
                                      backend: legacy.backend, model: model)
        var rewrite = read
        rewrite.id = "legacy-rewrite"
        rewrite.name = "默认改写"
        rewrite.direction = legacy.rewriteDirection
        rewrite.presetStyle = legacy.rewriteStyle
        return ProfileConfiguration(
            profiles: [read, rewrite],
            bindings: ProfileBindings(shortcut1ProfileId: legacy.readAction.usesPopup ? read.id : rewrite.id,
                                      shortcut2ProfileId: legacy.rewriteAction.usesPopup ? read.id : rewrite.id,
                                      selectionProfileId: read.id))
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
        var legacy = LegacyTranslationSettings()
        func stored<Value: RawRepresentable>(_ key: String) -> Value? where Value.RawValue == String {
            defaults.string(forKey: key).flatMap { Value(rawValue: $0) }
        }
        if let value: Language = stored("sourceLanguage") { legacy.nativeLanguage = value }
        if let value: Language = stored("targetLanguage") { legacy.foreignLanguage = value }
        if let value: TranslationDirection = stored(AppSettings.Keys.readDirection) { legacy.readDirection = value }
        if let value: TranslationDirection = stored(AppSettings.Keys.rewriteDirection) { legacy.rewriteDirection = value }
        if let value: RewriteStyle = stored(AppSettings.Keys.rewriteStyle) { legacy.rewriteStyle = value }
        if let value: TranslationBackend = stored(AppSettings.Keys.backend) { legacy.backend = value }
        if let value = defaults.string(forKey: AppSettings.Keys.openAIModel) { legacy.openAIModel = value }
        if let value = defaults.string(forKey: AppSettings.Keys.ollamaModel) { legacy.ollamaModel = value }
        if let value: ShortcutAction = stored(AppSettings.Keys.readShortcutAction) { legacy.readAction = value }
        if let value: ShortcutAction = stored(AppSettings.Keys.rewriteShortcutAction) { legacy.rewriteAction = value }
        return legacy
    }
}
