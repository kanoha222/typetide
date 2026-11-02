//
//  BackendSettingsView.swift
//  TypeTide
//
//  翻译后端设置：各 profile 自己选择 backend 与模型；这里汇总已绑定的 profile，并保留它们共享的连接
//  （内置模型下载、本地 Ollama、OpenAI 兼容端点与 API key）。
//

import SwiftUI

struct BackendSettingsView: View {
    @ObservedObject private var store = ProfileStore.shared
    @State private var baseURLDraft = AppSettings.openAIBaseURL
    @FocusState private var baseURLFocused: Bool
    @State private var openAIKey = AppSettings.openAIKey
    @State private var keyNotice: String?
    @State private var installedModels: [String] = []
    @State private var isLoadingModels = false
    @State private var ollamaError: String?
    @State private var isTesting = false
    @State private var results: [ProbeResult] = []

    struct ProbeResult: Identifiable {
        let id: String
        let title: String
        let result: BackendHealthResult
    }

    private var modelRecommendation: OllamaModelResolver.ModelRecommendation? {
        OllamaModelResolver.recommendation(from: installedModels)
    }

    /// Ollama models bound profiles use that are not installed: shown, never replaced.
    private var missingModels: [String] {
        var models: [String] = []
        for entry in ProfileEntry.allCases {
            guard let profile = store.configuration.profile(for: entry), profile.backend == .ollama,
                  !installedModels.contains(profile.model), !installedModels.contains(profile.model + ":latest"),
                  !models.contains(profile.model) else { continue }
            models.append(profile.model)
        }
        return models
    }

    var body: some View {
        Form {
            Section {
                ForEach(ProfileEntry.allCases) { entry in
                    LabeledContent(entry.label, value: summary(entry))
                }
                SettingsNote(text: "Each translation profile chooses its own backend and model in Profiles. This page keeps the connections they share.")
            } header: {
                SettingsSectionHeader(symbol: "server.rack", color: .teal,
                                      title: "Bound profiles", subtitle: "Where each entry's translations are generated")
            }

            Section { BuiltInModelSettingsView() } header: {
                SettingsSectionHeader(symbol: "cpu", color: .green, title: "Built-in translation", subtitle: "Runs privately on this Mac")
            }

            Section {
                HStack {
                    LabeledContent("Service", value: "\(OllamaConfig.host):\(OllamaConfig.port)")
                    Circle()
                        .fill(ollamaError == nil && !installedModels.isEmpty ? Color.green : Color.orange)
                        .frame(width: 8, height: 8)
                        .accessibilityLabel(ollamaError == nil ? "Connected" : "Unavailable")
                }
                if isLoadingModels {
                    HStack(spacing: 8) {
                        ProgressView().controlSize(.small)
                        Text("Checking Ollama…").foregroundStyle(.secondary)
                    }
                } else if installedModels.isEmpty {
                    SettingsNote(
                        text: ollamaError ?? "No generation models are installed. Install one with `ollama pull qwen2.5:7b`.",
                        symbol: "exclamationmark.triangle.fill",
                        tint: .orange
                    )
                } else {
                    LabeledContent("Installed", value: installedModels.joined(separator: ", "))
                    if let recommendation = modelRecommendation {
                        SettingsNote(
                            text: "Recommended for this Mac: \(recommendation.model) (comfortable target up to about \(Int(recommendation.maximumParameterBillions))B parameters).",
                            symbol: "memorychip.fill",
                            tint: .blue
                        )
                    }
                    ForEach(missingModels, id: \.self) { model in
                        SettingsNote(
                            text: "\(model) is used by a profile but is not installed. Pull it with `ollama pull \(model)`, or choose another model in Profiles.",
                            symbol: "exclamationmark.triangle.fill",
                            tint: .orange
                        )
                    }
                }
                HStack {
                    SettingsNote(text: "Embedding-only models are hidden because they cannot translate text.")
                    Spacer()
                    Button {
                        Task { await refreshModels() }
                    } label: {
                        Label("Refresh", systemImage: "arrow.clockwise")
                    }
                    .controlSize(.small)
                    .disabled(isLoadingModels)
                }
                SettingsNote(
                    text: "Ollama processes selected text on this Mac. TypeTide does not send it to a TypeTide service.",
                    symbol: "hand.raised.fill",
                    tint: .green
                )
            } header: {
                SettingsSectionHeader(symbol: "desktopcomputer", color: .green,
                                      title: "Ollama", subtitle: "Local and private")
            }

            Section {
                TextField("Base URL", text: $baseURLDraft)
                    .textFieldStyle(.roundedBorder)
                    .focused($baseURLFocused)
                    .onSubmit { commitBaseURL() }
                    .onChange(of: baseURLFocused) { _, focused in
                        if !focused { commitBaseURL() }
                    }
                SecureField("API Key", text: $openAIKey)
                    .textFieldStyle(.roundedBorder)
                    .onChange(of: openAIKey) { _, value in AppSettings.openAIKey = value }
                if let keyNotice {
                    SettingsNote(text: keyNotice, symbol: "key.fill", tint: .orange)
                }
                SettingsNote(text: "Works with any OpenAI-compatible /chat/completions endpoint (official API, proxies, local servers). The Base URL is saved when you press Return or leave the field.")
                SettingsNote(
                    text: "When a profile uses this endpoint, triggering a translation or rewrite sends the selected text to it. Avoid cloud profiles for passwords, private messages, or other sensitive text.",
                    symbol: "exclamationmark.shield.fill",
                    tint: .orange
                )
            } header: {
                SettingsSectionHeader(symbol: "cloud.fill", color: .indigo, title: "OpenAI-compatible")
            }

            Section {
                HStack {
                    Button {
                        Task { await testBoundModels() }
                    } label: {
                        if isTesting {
                            Label("Testing…", systemImage: "hourglass")
                        } else {
                            Label("Test bound models", systemImage: "stethoscope")
                        }
                    }
                    .disabled(isTesting)
                    Spacer()
                }
                ForEach(results) { item in
                    SettingsNote(
                        text: "\(item.title): \(describe(item.result))",
                        symbol: item.result.isSuccess ? "bolt.fill" : "wrench.and.screwdriver.fill",
                        tint: item.result.isSuccess ? .green : .orange
                    )
                }
            } header: {
                SettingsSectionHeader(symbol: "stethoscope", color: .blue,
                                      title: "Connection Test", subtitle: "Uses fixed synthetic text, never your clipboard")
            }
        }
        .settingsPage("Backend")
        .task { await refreshModels() }
        .onDisappear { commitBaseURL() }
    }

    private func summary(_ entry: ProfileEntry) -> String {
        guard let profile = store.configuration.profile(for: entry) else { return "No profile is bound" }
        let model = profile.backend == .builtIn ? "" : " \(profile.model)"
        return "\(profile.name) · \(profile.backend.displayName)\(model)"
    }

    /// Saves a new Base URL. An API key belongs to one origin, so moving to another origin clears it.
    private func commitBaseURL() {
        let url = baseURLDraft.trimmingCharacters(in: .whitespacesAndNewlines)
        let current = AppSettings.openAIBaseURL
        guard url != current else { return }
        if !openAIKey.isEmpty && !ProfileExecution.sameOrigin(current, url) {
            openAIKey = ""
            AppSettings.openAIKey = ""
            keyNotice = "The API key was cleared because the endpoint changed. Enter the key for the new endpoint."
        }
        AppSettings.openAIBaseURL = url
    }

    @MainActor
    private func testBoundModels() async {
        isTesting = true
        defer { isTesting = false }
        var tested: [ProbeResult] = []
        var seen = Set<String>()
        for entry in ProfileEntry.allCases {
            guard let profile = store.configuration.profile(for: entry) else { continue }
            let key = "\(profile.backend.rawValue)|\(profile.model)"
            guard seen.insert(key).inserted else { continue }
            let title = profile.backend == .builtIn ? "Built-in model" : "\(profile.backend.displayName) \(profile.model)"
            let result = await BackendHealthChecker.check(profile: profile)
            tested.append(ProbeResult(id: key, title: title, result: result))
        }
        results = tested
    }

    private func describe(_ result: BackendHealthResult) -> String {
        guard result.isSuccess else { return result.message }
        let first = result.firstTokenMilliseconds.map { "first token \($0) ms, " } ?? ""
        return "\(result.message) \(first)total \(result.totalMilliseconds) ms."
    }

    @MainActor
    private func refreshModels() async {
        isLoadingModels = true
        ollamaError = nil
        defer { isLoadingModels = false }

        let allModels = await OllamaModelResolver.installedModels()
        installedModels = allModels.filter { model in
            let name = model.lowercased()
            return !name.contains("embed") && !name.contains("bge") && !name.contains("minilm")
        }
        guard installedModels.isEmpty else { return }
        ollamaError = allModels.isEmpty
            ? "Ollama is not responding. Start Ollama, then refresh."
            : "Only embedding models are installed. Install a generation model to translate."
    }
}
