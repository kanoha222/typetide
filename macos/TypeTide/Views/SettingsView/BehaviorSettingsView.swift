//
//  BehaviorSettingsView.swift
//  TypeTide
//
//  触发与改写行为设置。风格与自定义提示词属于各个翻译 profile（Profiles 页）。
//

import SwiftUI

struct BehaviorSettingsView: View {
    @AppStorage(AppSettings.Keys.selectionTrigger) private var selectionRaw = SelectionTrigger.none.rawValue
    @AppStorage(AppSettings.Keys.rewritePreview) private var rewritePreview = false

    var body: some View {
        Form {
            Section {
                Picker("After selecting text", selection: $selectionRaw) {
                    ForEach(SelectionTrigger.allCases) { t in
                        Text(t.displayName).tag(t.rawValue)
                    }
                }
                .onChange(of: selectionRaw) { _, _ in
                    TriggerController.shared.applyEnabledState()
                }
                SettingsNote(text: "“Show floating icon” pops a small button next to your selection; “Auto-translate” shows the translation immediately. Both run the profile bound to Selection. The shortcut always works regardless.")

            } header: {
                SettingsSectionHeader(symbol: "text.viewfinder", color: .blue,
                                      title: "Selection translation", subtitle: "Choose what happens after selecting text")
            }

            Section {
                Toggle(isOn: $rewritePreview) {
                    SettingsLabel(symbol: "eye.fill", color: .teal, title: "Preview before replacing")
                }
                SettingsNote(text: rewritePreview
                             ? "Rewrite shows the result in a popup; click Replace to apply."
                             : "Rewrite replaces the text in place immediately (undo with ⌘Z).")
                SettingsNote(text: "Writing styles and custom system prompts are set per profile in Profiles.")
            } header: {
                SettingsSectionHeader(symbol: "pencil.and.outline", color: .purple,
                                      title: "Rewrite & replace", subtitle: "Control preview before replacing")
            }
        }
        .settingsPage("Behavior")
    }
}
