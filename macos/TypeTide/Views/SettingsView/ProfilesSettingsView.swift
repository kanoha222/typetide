//
//  ProfilesSettingsView.swift
//  TypeTide
//
//  Named translation profiles and their bindings (Shortcut 1, Shortcut 2, Selection). Edits stay
//  in a draft until Save, which validates and stores the whole configuration at once; a save is
//  refused when the profiles changed elsewhere (the menu bar) since the draft was loaded.
//

import SwiftUI

struct ProfilesSettingsView: View {
    @ObservedObject private var store = ProfileStore.shared
    @State private var draft = ProfileStore.shared.configuration
    @State private var baseline = ProfileStore.shared.configuration
    @State private var selectedId = ProfileStore.shared.configuration.profiles.first?.id ?? ""
    @State private var status: String?
    @State private var pendingDelete: TranslationProfile?
    @State private var isTesting = false

    private var isDirty: Bool { draft != baseline }
    private var selectedIndex: Int? { draft.profiles.firstIndex { $0.id == selectedId } }
    private var current: TranslationProfile? { selectedIndex.map { draft.profiles[$0] } }

    var body: some View {
        Form {
            if let error = store.loadError {
                Section {
                    SettingsNote(text: error, symbol: "exclamationmark.triangle.fill", tint: .orange)
                }
            }

            Section {
                Picker("Profile", selection: $selectedId) {
                    ForEach(draft.profiles) { profile in
                        Text(displayName(profile)).tag(profile.id)
                    }
                }
                HStack {
                    Button("Add") { addProfile() }
                        .disabled(draft.profiles.count >= ProfileConfiguration.maxProfiles)
                    Button("Duplicate") { duplicateProfile() }
                        .disabled(current == nil || draft.profiles.count >= ProfileConfiguration.maxProfiles)
                    Button("Delete", role: .destructive) { requestDelete() }
                        .disabled(current == nil || draft.profiles.count <= 1)
                    Spacer()
                }
            } header: {
                SettingsSectionHeader(symbol: "slider.horizontal.3", color: .orange,
                                      title: "Profiles", subtitle: "Named language, prompt and model choices")
            }

            if let profile = current {
                Section {
                    TextField("Name", text: field(\.name, ""))
                    Picker("Native language", selection: field(\.nativeLanguage, .chinese)) {
                        ForEach(Language.allCases) { Text($0.displayName).tag($0) }
                    }
                    Picker("Foreign language", selection: field(\.foreignLanguage, .english)) {
                        ForEach(Language.allCases) { Text($0.displayName).tag($0) }
                    }
                    Picker("Direction", selection: field(\.direction, .auto)) {
                        ForEach(TranslationDirection.allCases) { direction in
                            Text(direction.label(native: profile.nativeLanguage, foreign: profile.foreignLanguage))
                                .tag(direction)
                        }
                    }
                    Picker("Backend", selection: field(\.backend, .builtIn)) {
                        ForEach(TranslationBackend.allCases) { Text($0.displayName).tag($0) }
                    }
                    if profile.backend != .builtIn {
                        TextField("Model", text: field(\.model, ""))
                    }
                    SettingsNote(text: routeText(profile), symbol: profile.backend == .openai ? "cloud.fill" : "lock.fill",
                                 tint: profile.backend == .openai ? .orange : .green)
                } header: {
                    SettingsSectionHeader(symbol: "globe", color: .green,
                                          title: "Languages & model", subtitle: "Used by every entry bound to this profile")
                }

                Section {
                    Picker("Instructions", selection: field(\.promptMode, .preset)) {
                        Text("Preset style").tag(PromptMode.preset)
                        Text("Custom system prompt").tag(PromptMode.custom)
                    }
                    .pickerStyle(.segmented)
                    if profile.promptMode == .preset {
                        Picker("Style", selection: field(\.presetStyle, .faithful)) {
                            ForEach(RewriteStyle.allCases) { Text($0.displayName).tag($0) }
                        }
                    } else {
                        TextEditor(text: field(\.systemPrompt, ""))
                            .font(.body.monospaced())
                            .frame(minHeight: 120)
                            .accessibilityLabel("Custom system prompt")
                        SettingsNote(text: "Replaces the default instructions entirely. Variables: {{source_language}} and {{target_language}} (exact, case-sensitive); anything else stays literal. The selected text is sent separately as the user message.")
                    }
                    if let problem = ProfileExecution.capabilityError(for: profile) {
                        SettingsNote(text: problem, symbol: "exclamationmark.triangle.fill", tint: .orange)
                    }
                    Text(preview(profile))
                        .font(.caption.monospaced())
                        .foregroundStyle(.secondary)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    HStack {
                        Button(isTesting ? "Testing…" : "Test fixed text") { Task { await testProfile(profile) } }
                            .disabled(isTesting)
                        Spacer()
                    }
                } header: {
                    SettingsSectionHeader(symbol: "text.bubble.fill", color: .purple,
                                          title: "Prompt", subtitle: "Preset style or your own system prompt")
                }
            }

            Section {
                ForEach(ProfileEntry.allCases) { entry in
                    Picker(entryTitle(entry), selection: bindingField(entry)) {
                        ForEach(draft.profiles) { profile in
                            Text(displayName(profile)).tag(profile.id)
                        }
                    }
                }
                SettingsNote(text: "Each entry runs its bound profile; a shortcut's action (Shortcuts page) still chooses popup or replace and smart, native or foreign target.")
            } header: {
                SettingsSectionHeader(symbol: "keyboard.fill", color: .pink,
                                      title: "Bindings", subtitle: "Which profile each entry runs")
            }

            Section {
                HStack {
                    Button("Revert") { reload() }
                        .disabled(!isDirty)
                    Spacer()
                    if let status {
                        Text(status)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    Button("Save") { save() }
                        .buttonStyle(.borderedProminent)
                        .disabled(!isDirty)
                }
            }
        }
        .settingsPage("Profiles")
        .onReceive(store.$configuration) { committed in
            // Changes made elsewhere (menu bar) reach an editor without unsaved edits at once.
            if !isDirty && committed != baseline {
                draft = committed
                baseline = committed
                keepSelectionValid()
            }
        }
        .confirmationDialog("This profile is in use",
                            isPresented: Binding(get: { pendingDelete != nil },
                                                 set: { if !$0 { pendingDelete = nil } }),
                            titleVisibility: .visible) {
            ForEach(replacements) { replacement in
                Button("Move its bindings to \(displayName(replacement))") {
                    if let victim = pendingDelete { delete(victim, movingBindingsTo: replacement.id) }
                    pendingDelete = nil
                }
            }
            Button("Cancel", role: .cancel) { pendingDelete = nil }
        } message: {
            Text("Choose the profile its shortcuts and the selection trigger move to.")
        }
    }

    // MARK: - Draft editing

    /// A binding to one field of the selected profile, looked up by id on every access.
    private func field<Value>(_ keyPath: WritableKeyPath<TranslationProfile, Value>, _ fallback: Value) -> Binding<Value> {
        Binding(
            get: { current.map { $0[keyPath: keyPath] } ?? fallback },
            set: { newValue in
                guard let index = selectedIndex else { return }
                draft.profiles[index][keyPath: keyPath] = newValue
                status = nil
            }
        )
    }

    private func bindingField(_ entry: ProfileEntry) -> Binding<String> {
        Binding(
            get: { draft.bindings.profileId(for: entry) },
            set: { newValue in
                draft = draft.withBinding(entry, to: newValue)
                status = nil
            }
        )
    }

    private var replacements: [TranslationProfile] {
        draft.profiles.filter { $0.id != pendingDelete?.id }
    }

    private func newId() -> String { UUID().uuidString.lowercased() }

    private func addProfile() {
        var profile = ProfileConfiguration.defaults.profiles[0]
        profile.id = newId()
        profile.name = "New profile \(draft.profiles.count + 1)"
        draft.profiles.append(profile)
        selectedId = profile.id
        status = nil
    }

    private func duplicateProfile() {
        guard var copy = current else { return }
        copy.id = newId()
        copy.name = String((copy.name + " copy").utf16.prefix(ProfileConfiguration.maxNameUTF16)) ?? copy.name
        draft.profiles.append(copy)
        selectedId = copy.id
        status = nil
    }

    private func requestDelete() {
        guard let profile = current, draft.profiles.count > 1 else { return }
        let bound = ProfileEntry.allCases.contains { draft.bindings.profileId(for: $0) == profile.id }
        if bound {
            pendingDelete = profile
        } else {
            delete(profile, movingBindingsTo: nil)
        }
    }

    private func delete(_ profile: TranslationProfile, movingBindingsTo replacementId: String?) {
        var next = draft
        if let replacementId {
            for entry in ProfileEntry.allCases where next.bindings.profileId(for: entry) == profile.id {
                next = next.withBinding(entry, to: replacementId)
            }
        }
        next.profiles.removeAll { $0.id == profile.id }
        selectedId = replacementId ?? next.profiles.first?.id ?? ""
        draft = next
        status = nil
    }

    private func keepSelectionValid() {
        if selectedIndex == nil { selectedId = draft.profiles.first?.id ?? "" }
    }

    private func reload() {
        draft = store.configuration
        baseline = store.configuration
        keepSelectionValid()
        status = nil
    }

    private func save() {
        guard store.configuration == baseline else {
            status = "Profiles or bindings changed elsewhere. Revert to reload; your draft was not saved."
            return
        }
        if let error = store.save(draft) {
            status = "Not saved: \(error)"
            return
        }
        baseline = store.configuration
        draft = store.configuration
        status = "Saved."
    }

    @MainActor
    private func testProfile(_ profile: TranslationProfile) async {
        isTesting = true
        defer { isTesting = false }
        let route = ProfileExecution.route(for: profile, action: .smartPopup, text: ProfileExecution.testText)
        do {
            let output = try await TranslationService.shared.translateFully(
                text: ProfileExecution.testText, profile: profile, route: route, useCache: false)
            status = "Test passed: \(output.prefix(120))"
        } catch {
            status = "Test failed: \((error as? TranslationError)?.errorDescription ?? error.localizedDescription)"
        }
    }

    // MARK: - Display

    private func displayName(_ profile: TranslationProfile) -> String {
        ProfileConfiguration.isBlank(profile.name) ? "Untitled profile" : profile.name
    }

    private func entryTitle(_ entry: ProfileEntry) -> String {
        switch entry {
        case .shortcut1: return "Shortcut 1 (\(AppSettings.readShortcut?.displayString ?? "not set"))"
        case .shortcut2: return "Shortcut 2 (\(AppSettings.rewriteShortcut?.displayString ?? "not set"))"
        case .selection: return "Selection icon / auto-translate"
        }
    }

    private func routeText(_ profile: TranslationProfile) -> String {
        switch profile.backend {
        case .builtIn:
            return "Local: runs with the built-in model on this Mac."
        case .ollama:
            return ProfileExecution.requiresPreview(profile, ollamaHost: AppSettings.ollamaHost)
                ? "Network, unencrypted: selected text goes to the Ollama host as plain text. Replace always shows the popup first."
                : "Local: runs on Ollama on this Mac."
        case .openai:
            return "Cloud: selected text is sent to the configured API endpoint (Backend page)."
        }
    }

    /// The effective request for the fixed sample text.
    private func preview(_ profile: TranslationProfile) -> String {
        let route = ProfileExecution.route(for: profile, action: .smartPopup, text: ProfileExecution.testText)
        if let system = ProfileExecution.systemPrompt(for: profile, route: route) {
            return "System prompt:\n\(system)\n\nUser message: the selected text, unchanged."
        }
        switch profile.backend {
        case .builtIn:
            return "Built-in model: faithful translation into \(route.target.promptName) (TranslateGemma template)."
        case .ollama:
            return "Prompt:\n" + TranslationPrompt.plain("<selected text>", target: route.target, source: route.source,
                                                          style: profile.presetStyle)
        case .openai:
            return "System prompt:\n" + TranslationPrompt.system(target: route.target, source: route.source,
                                                                 style: profile.presetStyle)
                + "\n\nUser message: the selected text, unchanged."
        }
    }
}
