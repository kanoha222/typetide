//
//  TranslationService.swift
//  TypeTide
//
//  Runs a translation profile on its provider, with caching and streaming output.
//

import Foundation

@MainActor
final class TranslationService {
    static let shared = TranslationService()
    private init() {}

#if DEBUG
    /// XCTest seam used to keep network behavior deterministic.
    var providerOverride: TranslationProvider?
#endif

    /// The profile used outside a shortcut or the selection trigger (the connection test, the
    /// Services menu): the Shortcut 1 profile, or the first profile.
    var primaryProfile: TranslationProfile {
        let configuration = ProfileStore.shared.configuration
        return configuration.profile(for: .shortcut1) ?? configuration.profiles.first
            ?? ProfileConfiguration.defaults.profiles[0]
    }

    func provider(for profile: TranslationProfile) -> TranslationProvider {
#if DEBUG
        if let providerOverride { return providerOverride }
#endif
        switch profile.backend {
        case .builtIn: return LocalTranslationProvider()
        case .ollama: return OllamaProvider(model: profile.model)
        case .openai: return OpenAIProvider(model: profile.model)
        }
    }

    /// The primary profile's backend and model with a preset `style` (connection test, Services menu).
    func stream(text: String, from: Language?, to: Language, style: RewriteStyle = .faithful) -> AsyncThrowingStream<String, Error> {
        stream(text: text, profile: Self.preset(primaryProfile, style: style), route: TranslationRoute(source: from, target: to))
    }

    /// Streams `profile` translating `text` along `route`. A cache hit yields the delivered text at
    /// once. `useCache: false` (profile and connection tests) neither reads nor writes the cache.
    func stream(text: String, profile: TranslationProfile, route: TranslationRoute,
                useCache: Bool = true) -> AsyncThrowingStream<String, Error> {
        let provider = self.provider(for: profile)
        let systemPrompt = ProfileExecution.systemPrompt(for: profile, route: route)
        let style: RewriteStyle = profile.promptMode == .preset ? profile.presetStyle : .faithful
        let request = TranslationRequest(text: text, source: route.source, target: route.target, style: style,
                                         systemPrompt: systemPrompt)
        let cacheKey = TranslationCache.shared.key(
            backend: provider.id, from: route.source, to: route.target,
            text: Self.identity([Self.endpoint(of: profile), profile.model, profile.promptMode.rawValue,
                                 style.rawValue, systemPrompt ?? "", text]))
        let characterCount = text.count

        return AsyncThrowingStream { continuation in
            let start = ContinuousClock.now
            if let problem = ProfileExecution.capabilityError(for: profile) {
                continuation.finish(throwing: TranslationError.notConfigured(problem))
                return
            }
            if useCache, let cached = TranslationCache.shared.value(for: cacheKey) {
                continuation.yield(cached)
                continuation.finish()
                Self.recordTranslation(outcome: "cacheHit", provider: provider.id, start: start,
                                       firstToken: start, characterCount: characterCount)
                return
            }
            let task = Task {
                var full = ""
                var firstToken: ContinuousClock.Instant?
                do {
                    for try await delta in provider.stream(request) {
                        if firstToken == nil { firstToken = .now }
                        full += delta
                        continuation.yield(delta)
                    }
                    // Custom output is cached exactly; preset output keeps the legacy trim.
                    guard let delivered = ProfileExecution.deliveredOutput(for: profile, raw: full) else {
                        throw TranslationError.empty
                    }
                    if useCache { TranslationCache.shared.set(delivered, for: cacheKey) }
                    continuation.finish()
                    Self.recordTranslation(outcome: "success", provider: provider.id, start: start,
                                           firstToken: firstToken, characterCount: characterCount)
                } catch is CancellationError {
                    continuation.finish(throwing: CancellationError())
                    Self.recordTranslation(outcome: "cancelled", provider: provider.id, start: start,
                                           firstToken: firstToken, characterCount: characterCount)
                } catch {
                    continuation.finish(throwing: error)
                    Self.recordTranslation(outcome: "failure", provider: provider.id, start: start,
                                           firstToken: firstToken, characterCount: characterCount,
                                           errorCategory: Self.errorCategory(error))
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }

    /// The whole delivered result (for replacement): never a partial or blank text.
    func translateFully(text: String, profile: TranslationProfile, route: TranslationRoute,
                        useCache: Bool = true) async throws -> String {
        var full = ""
        for try await delta in stream(text: text, profile: profile, route: route, useCache: useCache) {
            full += delta
        }
        try Task.checkCancellation()
        guard let delivered = ProfileExecution.deliveredOutput(for: profile, raw: full) else {
            throw TranslationError.empty
        }
        return delivered
    }

    /// The primary profile's backend and model with a preset `style`.
    func translateFully(text: String, from: Language?, to: Language, style: RewriteStyle = .faithful) async throws -> String {
        try await translateFully(text: text, profile: Self.preset(primaryProfile, style: style),
                                 route: TranslationRoute(source: from, target: to))
    }

    private static func preset(_ profile: TranslationProfile, style: RewriteStyle) -> TranslationProfile {
        var copy = profile
        copy.promptMode = .preset
        copy.presetStyle = style
        return copy
    }

    /// Where the provider sends the text: a different endpoint never shares cached results.
    private static func endpoint(of profile: TranslationProfile) -> String {
        switch profile.backend {
        case .builtIn: return "builtin"
        case .ollama: return "\(AppSettings.ollamaHost):\(AppSettings.ollamaPort)"
        case .openai: return AppSettings.openAIBaseURL
        }
    }

    /// Length-prefixed fields, so no prompt or text can forge another identity.
    private static func identity(_ fields: [String]) -> String {
        fields.map { "\($0.utf8.count):\($0)" }.joined()
    }

    private static func recordTranslation(
        outcome: String,
        provider: String,
        start: ContinuousClock.Instant,
        firstToken: ContinuousClock.Instant?,
        characterCount: Int,
        errorCategory: String? = nil
    ) {
        let event = DiagnosticEvent(
            name: .translation,
            outcome: outcome,
            backend: provider,
            errorCategory: errorCategory,
            firstTokenMilliseconds: firstToken.map { start.duration(to: $0).milliseconds },
            totalMilliseconds: start.duration(to: .now).milliseconds,
            inputCharacterCount: characterCount
        )
        Task { await LocalDiagnostics.shared.record(event) }
    }

    private static func errorCategory(_ error: Error) -> String {
        if error is CancellationError { return "cancelled" }
        if let error = error as? TranslationError {
            return error.category
        }
        return "provider"
    }
}
