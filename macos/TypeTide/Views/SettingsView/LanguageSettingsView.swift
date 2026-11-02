//
//  LanguageSettingsView.swift
//  TypeTide
//
//  Created by eevv on 10/10/25.
//
//  Languages, direction and style belong to each translation profile; this page summarizes the
//  profiles bound to Shortcut 1, Shortcut 2 and the selection trigger.
//

import SwiftUI

struct LanguageSettingsView: View {
    @ObservedObject private var store = ProfileStore.shared

    var body: some View {
        Form {
            Section {
                ForEach(ProfileEntry.allCases) { entry in
                    LabeledContent(entry.label, value: summary(entry))
                }
                SettingsNote(text: "Languages, translation direction and writing style or a custom system prompt belong to each translation profile. Edit them in Profiles.")
            } header: {
                SettingsSectionHeader(symbol: "globe", color: .green,
                                      title: "Languages", subtitle: "Per translation profile")
            }
        }
        .settingsPage("Language")
    }

    private func summary(_ entry: ProfileEntry) -> String {
        guard let profile = store.configuration.profile(for: entry) else { return "No profile is bound" }
        let native = profile.nativeLanguage.shortName
        let foreign = profile.foreignLanguage.shortName
        let pair: String
        switch profile.direction {
        case .auto: pair = "\(native) ⇄ \(foreign)"
        case .nativeToForeign: pair = "\(native) → \(foreign)"
        case .foreignToNative: pair = "\(foreign) → \(native)"
        }
        let style = profile.promptMode == .custom ? "custom prompt" : profile.presetStyle.displayName
        return "\(profile.name) · \(pair) · \(style)"
    }
}
