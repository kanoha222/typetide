import XCTest
@testable import TypeTide

/// Records each request and answers with a fixed reply.
private final class RecordingProvider: TranslationProvider {
    let id = "recording-test-provider"
    let reply: String
    private(set) var requests: [TranslationRequest] = []

    init(reply: String) { self.reply = reply }

    func stream(_ request: TranslationRequest) -> AsyncThrowingStream<String, Error> {
        requests.append(request)
        let reply = self.reply
        return AsyncThrowingStream { continuation in
            continuation.yield(reply)
            continuation.finish()
        }
    }
}

/// Running a profile through TranslationService: prompts, models, exact output, cache isolation.
@MainActor
final class TranslationServiceProfileTests: XCTestCase {
    override func tearDown() {
        TranslationService.shared.providerOverride = nil
        super.tearDown()
    }

    private func profile(backend: TranslationBackend = .ollama, mode: PromptMode = .preset,
                         style: RewriteStyle = .faithful) -> TranslationProfile {
        TranslationProfile(id: "service-test", name: "Service test", nativeLanguage: .chinese, foreignLanguage: .english,
                           direction: .auto, promptMode: mode, presetStyle: style,
                           systemPrompt: "Translate {{source_language}} into {{target_language}}.",
                           backend: backend, model: "synthetic-model")
    }

    private let route = TranslationRoute(source: .chinese, target: .english)

    func testCustomProfileSendsItsRenderedPromptAndTheTextUnchanged() async throws {
        let provider = RecordingProvider(reply: "ok")
        TranslationService.shared.providerOverride = provider
        let text = "custom prompt request \(UUID().uuidString)"
        _ = try await TranslationService.shared.translateFully(text: text, profile: profile(mode: .custom), route: route)
        let request = try XCTUnwrap(provider.requests.last)
        XCTAssertEqual(request.systemPrompt, "Translate Chinese into English.")
        XCTAssertEqual(request.text, text)
        XCTAssertEqual(request.target, .english)
    }

    func testPresetProfileKeepsTheBuiltInInstructions() async throws {
        let provider = RecordingProvider(reply: "ok")
        TranslationService.shared.providerOverride = provider
        _ = try await TranslationService.shared.translateFully(text: "preset request \(UUID().uuidString)",
                                                               profile: profile(style: .formal), route: route)
        let request = try XCTUnwrap(provider.requests.last)
        XCTAssertNil(request.systemPrompt)
        XCTAssertEqual(request.style, .formal)
    }

    func testCustomOutputIsExactAndPresetOutputIsTrimmed() async throws {
        TranslationService.shared.providerOverride = RecordingProvider(reply: "  EXACT\n\tline 2\n")
        let custom = try await TranslationService.shared.translateFully(
            text: "exact custom \(UUID().uuidString)", profile: profile(mode: .custom), route: route)
        XCTAssertEqual(custom, "  EXACT\n\tline 2\n")
        let preset = try await TranslationService.shared.translateFully(
            text: "exact preset \(UUID().uuidString)", profile: profile(), route: route)
        XCTAssertEqual(preset, "EXACT\n\tline 2")
    }

    func testCacheSeparatesPromptsAndTestsSkipIt() async throws {
        let provider = RecordingProvider(reply: "cached")
        TranslationService.shared.providerOverride = provider
        let text = "cache identity \(UUID().uuidString)"
        _ = try await TranslationService.shared.translateFully(text: text, profile: profile(), route: route)
        _ = try await TranslationService.shared.translateFully(text: text, profile: profile(), route: route)
        XCTAssertEqual(provider.requests.count, 1, "the same profile and text is a cache hit")
        _ = try await TranslationService.shared.translateFully(text: text, profile: profile(mode: .custom), route: route)
        XCTAssertEqual(provider.requests.count, 2, "a custom prompt never reuses a preset result")
        _ = try await TranslationService.shared.translateFully(text: text, profile: profile(), route: route, useCache: false)
        XCTAssertEqual(provider.requests.count, 3, "tests never answer from the cache")
    }

    func testBuiltInRefusesWhatItCannotRun() async {
        TranslationService.shared.providerOverride = RecordingProvider(reply: "never")
        do {
            _ = try await TranslationService.shared.translateFully(
                text: "capability \(UUID().uuidString)", profile: profile(backend: .builtIn, mode: .custom), route: route)
            XCTFail("A built-in custom profile must be refused")
        } catch let error as TranslationError {
            XCTAssertEqual(error.category, "configuration")
        } catch {
            XCTFail("Unexpected error: \(error)")
        }
    }

    func testEachProfileUsesItsOwnModel() throws {
        TranslationService.shared.providerOverride = nil
        let ollama = try XCTUnwrap(TranslationService.shared.provider(for: profile(backend: .ollama)) as? OllamaProvider)
        XCTAssertEqual(ollama.model, "synthetic-model")
        let cloud = try XCTUnwrap(TranslationService.shared.provider(for: profile(backend: .openai)) as? OpenAIProvider)
        XCTAssertEqual(cloud.model, "synthetic-model")
    }

    func testApiKeyBelongsToOneOrigin() {
        XCTAssertTrue(ProfileExecution.sameOrigin("https://api.example.test/v1", "https://API.example.test/other"))
        XCTAssertTrue(ProfileExecution.sameOrigin("https://api.example.test", "https://api.example.test:443/v1"))
        XCTAssertFalse(ProfileExecution.sameOrigin("https://api.example.test/v1", "https://openrouter.example.test/v1"))
        XCTAssertFalse(ProfileExecution.sameOrigin("http://127.0.0.1:8080", "https://127.0.0.1:8080"))
        XCTAssertFalse(ProfileExecution.sameOrigin("http://127.0.0.1:8080", "http://127.0.0.1:8081"))
        XCTAssertFalse(ProfileExecution.sameOrigin("", "https://api.example.test"))
    }
}
