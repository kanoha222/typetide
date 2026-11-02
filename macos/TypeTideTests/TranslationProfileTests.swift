import XCTest
@testable import TypeTide

/// Profile model, validation, legacy migration and bindings: the same contract as the Windows app.
final class TranslationProfileTests: XCTestCase {
    private func profile(_ id: String, backend: TranslationBackend = .builtIn, model: String = "",
                         mode: PromptMode = .preset, prompt: String = "") -> TranslationProfile {
        TranslationProfile(id: id, name: "Profile \(id)", nativeLanguage: .chinese, foreignLanguage: .english,
                           direction: .auto, promptMode: mode, presetStyle: .faithful, systemPrompt: prompt,
                           backend: backend, model: model)
    }

    private func configuration(_ profiles: [TranslationProfile]) -> ProfileConfiguration {
        let first = profiles.first?.id ?? ""
        return ProfileConfiguration(profiles: profiles,
                                    bindings: ProfileBindings(shortcut1ProfileId: first, shortcut2ProfileId: first,
                                                              selectionProfileId: first))
    }

    func testDefaultsAreValidAndBound() {
        let defaults = ProfileConfiguration.defaults
        XCTAssertNil(defaults.validationError())
        XCTAssertEqual(defaults.profile(for: .shortcut1)?.id, "default-read")
        XCTAssertEqual(defaults.profile(for: .shortcut2)?.id, "default-rewrite")
        XCTAssertEqual(defaults.profile(for: .selection)?.id, "default-read")
        XCTAssertEqual(defaults.profile(for: .shortcut2)?.direction, .nativeToForeign)
    }

    func testValidationRejectsWhatWindowsRejects() {
        var named = profile("a")
        named.name = "   "
        XCTAssertNotNil(configuration([named]).validationError(), "blank name")
        named.name = String(repeating: "n", count: ProfileConfiguration.maxNameUTF16 + 1)
        XCTAssertNotNil(configuration([named]).validationError(), "name over 80 UTF-16 units")
        named.name = String(repeating: "n", count: ProfileConfiguration.maxNameUTF16)
        XCTAssertNil(configuration([named]).validationError(), "name at the limit")

        XCTAssertNotNil(configuration([profile("a", backend: .ollama, mode: .custom, prompt: " \n\t")]).validationError(),
                        "blank custom prompt")
        let oversize = String(repeating: "x", count: ProfileConfiguration.maxPromptUTF16 + 1)
        XCTAssertNotNil(configuration([profile("a", backend: .ollama, model: "m", mode: .custom, prompt: oversize)])
                            .validationError(), "custom prompt over 32 Ki UTF-16 units")
        XCTAssertNil(configuration([profile("a", backend: .ollama, model: "m", mode: .custom, prompt: "Translate.")])
                         .validationError(), "a custom Ollama profile with a model")
        XCTAssertNotNil(configuration([profile("a", backend: .openai, model: "  ")]).validationError(), "blank remote model")
        XCTAssertNil(configuration([profile("a")]).validationError(), "the built-in model needs no model name")
        XCTAssertNotNil(configuration([profile("a"), profile("a")]).validationError(), "duplicate ids")
        var dangling = configuration([profile("a")])
        dangling.bindings.selectionProfileId = "missing"
        XCTAssertNotNil(dangling.validationError(), "a binding to a missing profile")
        XCTAssertNotNil(configuration([]).validationError(), "no profiles")
        let tooMany = (0...ProfileConfiguration.maxProfiles).map { profile("p\($0)") }
        XCTAssertNotNil(configuration(tooMany).validationError(), "more than 64 profiles")
    }

    func testMigrationBindsEachSlotByItsAction() {
        for first in ShortcutAction.allCases {
            for second in ShortcutAction.allCases {
                var legacy = LegacyTranslationSettings()
                legacy.readAction = first
                legacy.rewriteAction = second
                let migrated = ProfileConfiguration.migrated(from: legacy)
                XCTAssertNil(migrated.validationError())
                XCTAssertEqual(migrated.bindings.shortcut1ProfileId, first.usesPopup ? "legacy-read" : "legacy-rewrite")
                XCTAssertEqual(migrated.bindings.shortcut2ProfileId, second.usesPopup ? "legacy-read" : "legacy-rewrite")
                XCTAssertEqual(migrated.bindings.selectionProfileId, "legacy-read")
            }
        }
    }

    func testMigrationKeepsLegacyChoices() throws {
        var legacy = LegacyTranslationSettings()
        legacy.nativeLanguage = .spanish
        legacy.foreignLanguage = .french
        legacy.readDirection = .foreignToNative
        legacy.rewriteDirection = .nativeToForeign
        legacy.rewriteStyle = .formal
        legacy.backend = .openai
        legacy.openAIModel = "synthetic-api-model"
        let migrated = ProfileConfiguration.migrated(from: legacy)
        let read = try XCTUnwrap(migrated.profiles.first { $0.id == "legacy-read" })
        let rewrite = try XCTUnwrap(migrated.profiles.first { $0.id == "legacy-rewrite" })
        XCTAssertEqual(read.nativeLanguage, .spanish)
        XCTAssertEqual(read.foreignLanguage, .french)
        XCTAssertEqual(read.direction, .foreignToNative)
        XCTAssertEqual(read.presetStyle, .faithful, "reading always translated faithfully")
        XCTAssertEqual(rewrite.direction, .nativeToForeign)
        XCTAssertEqual(rewrite.presetStyle, .formal)
        XCTAssertEqual(read.backend, .openai)
        XCTAssertEqual(rewrite.model, "synthetic-api-model")
        legacy.backend = .ollama
        legacy.ollamaModel = "   "
        XCTAssertEqual(ProfileConfiguration.migrated(from: legacy).profiles.first?.model, "qwen2.5:3b",
                       "a blank legacy model migrates to the old default instead of an invalid profile")
    }

    func testWithBindingChangesOnlyThatEntry() {
        let base = configuration([profile("a"), profile("b")])
        let bound = base.withBinding(.shortcut2, to: "b")
        XCTAssertEqual(bound.bindings.shortcut2ProfileId, "b")
        XCTAssertEqual(bound.bindings.shortcut1ProfileId, "a")
        XCTAssertEqual(bound.bindings.selectionProfileId, "a")
        XCTAssertEqual(bound.profiles, base.profiles)
        XCTAssertEqual(base.bindings.shortcut2ProfileId, "a", "the original is not modified")
    }

    func testCodableRoundTripKeepsPromptsExactly() throws {
        var custom = profile("c", backend: .ollama, model: "m", mode: .custom,
                             prompt: "Line 1\r\n\tLine 2  \n\u{4F60}\u{597D} \u{1F600}")
        custom.name = "Mixed \u{540D}\u{5B57} & more"
        let original = configuration([custom])
        let decoded = try JSONDecoder().decode(ProfileConfiguration.self, from: JSONEncoder().encode(original))
        XCTAssertEqual(decoded, original)
    }
}

/// Executing a profile: direction, prompt rendering, capability, delivered output, replacement.
final class ProfileExecutionTests: XCTestCase {
    private func profile(native: Language = .spanish, foreign: Language = .french,
                         direction: TranslationDirection = .nativeToForeign, backend: TranslationBackend = .ollama,
                         mode: PromptMode = .preset, style: RewriteStyle = .faithful) -> TranslationProfile {
        TranslationProfile(id: "p", name: "P", nativeLanguage: native, foreignLanguage: foreign, direction: direction,
                           promptMode: mode, presetStyle: style, systemPrompt: "Translate.", backend: backend,
                           model: "m")
    }

    func testRouteFollowsTheProfileAndTheAction() {
        let fixed = profile()
        XCTAssertEqual(ProfileExecution.route(for: fixed, action: .smartPopup, text: "hello"),
                       TranslationRoute(source: .spanish, target: .french), "a fixed direction skips detection")
        XCTAssertEqual(ProfileExecution.route(for: fixed, action: .nativePopup, text: "hello").target, .spanish)
        XCTAssertEqual(ProfileExecution.route(for: fixed, action: .foreignReplace, text: "hello").target, .french)
        let auto = profile(native: .chinese, foreign: .english, direction: .auto)
        XCTAssertEqual(ProfileExecution.route(for: auto, action: .smartPopup, text: "\u{4F60}\u{597D}\u{4E16}\u{754C}").target,
                       .english, "auto: native text goes to the foreign language")
    }

    func testCustomPromptRendersOnceAndCaseSensitively() {
        let rendered = ProfileExecution.renderCustomPrompt(
            "From {{source_language}} to {{target_language}}; again {{source_language}}. {{Target_language}} {{other}}",
            source: .chinese, target: .english)
        XCTAssertEqual(rendered, "From Chinese to English; again Chinese. {{Target_language}} {{other}}")
        XCTAssertEqual(ProfileExecution.renderCustomPrompt("{{source_language}}", source: nil, target: .english),
                       "auto-detected language")
        XCTAssertEqual(ProfileExecution.renderCustomPrompt("{{{{target_language}}}}", source: nil, target: .french),
                       "{{French}}", "a substituted value is never scanned again")
    }

    func testSystemPromptOnlyForCustomProfiles() {
        let route = TranslationRoute(source: .chinese, target: .english)
        XCTAssertNil(ProfileExecution.systemPrompt(for: profile(), route: route))
        var custom = profile(mode: .custom)
        custom.systemPrompt = "Render {{target_language}}"
        XCTAssertEqual(ProfileExecution.systemPrompt(for: custom, route: route), "Render English")
    }

    func testBuiltInCapability() {
        XCTAssertNotNil(ProfileExecution.capabilityError(for: profile(backend: .builtIn, mode: .custom)))
        XCTAssertNotNil(ProfileExecution.capabilityError(for: profile(backend: .builtIn, style: .formal)))
        XCTAssertNil(ProfileExecution.capabilityError(for: profile(backend: .builtIn)))
        XCTAssertNil(ProfileExecution.capabilityError(for: profile(backend: .ollama, mode: .custom)))
    }

    func testDeliveredOutputAndReplacementText() {
        let raw = "  line one\n\tline two\n"
        XCTAssertEqual(ProfileExecution.deliveredOutput(for: profile(mode: .custom), raw: raw), raw)
        XCTAssertEqual(ProfileExecution.deliveredOutput(for: profile(), raw: raw), "line one\n\tline two")
        XCTAssertNil(ProfileExecution.deliveredOutput(for: profile(mode: .custom), raw: " \n\t"))
        XCTAssertEqual(ProfileExecution.replacementText(raw, source: "selected"), "  line one\n\tline two")
        XCTAssertEqual(ProfileExecution.replacementText(raw, source: "selected\n"), raw)
        XCTAssertEqual(ProfileExecution.replacementText("text\r\n", source: "selected"), "text")
    }

    func testUnencryptedNetworkOllamaRequiresPreview() {
        XCTAssertTrue(ProfileExecution.requiresPreview(profile(backend: .ollama), ollamaHost: "http://192.0.2.10"))
        XCTAssertFalse(ProfileExecution.requiresPreview(profile(backend: .ollama), ollamaHost: "http://127.0.0.1"))
        XCTAssertFalse(ProfileExecution.requiresPreview(profile(backend: .ollama), ollamaHost: "http://localhost"))
        XCTAssertFalse(ProfileExecution.requiresPreview(profile(backend: .ollama), ollamaHost: "https://ollama.example.test"))
        XCTAssertFalse(ProfileExecution.requiresPreview(profile(backend: .openai), ollamaHost: "http://192.0.2.10"))
    }
}

/// Persistence in UserDefaults: first-run migration, invalid data and validated saves.
final class ProfileStoreTests: XCTestCase {
    private var suiteName = ""
    private var defaults: UserDefaults!

    override func setUp() {
        super.setUp()
        suiteName = "TypeTideProfileStoreTests.\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
    }

    override func tearDown() {
        defaults.removePersistentDomain(forName: suiteName)
        defaults = nil
        super.tearDown()
    }

    func testFirstLoadMigratesLegacySettingsAndSavesThem() throws {
        defaults.set("es", forKey: "sourceLanguage")
        defaults.set("fr", forKey: "targetLanguage")
        defaults.set("formal", forKey: AppSettings.Keys.rewriteStyle)
        defaults.set("smartReplace", forKey: AppSettings.Keys.readShortcutAction)
        let store = ProfileStore(defaults: defaults)
        XCTAssertNil(store.loadError)
        XCTAssertEqual(store.configuration.profiles.first?.nativeLanguage, .spanish)
        XCTAssertEqual(store.configuration.bindings.shortcut1ProfileId, "legacy-rewrite")
        let saved = try XCTUnwrap(defaults.data(forKey: ProfileStore.storageKey))
        XCTAssertEqual(try JSONDecoder().decode(ProfileConfiguration.self, from: saved), store.configuration)
    }

    func testUnreadableSavedProfilesAreReportedAndNotOverwritten() {
        let garbage = Data("not json".utf8)
        defaults.set(garbage, forKey: ProfileStore.storageKey)
        let store = ProfileStore(defaults: defaults)
        XCTAssertNotNil(store.loadError)
        XCTAssertNil(store.configuration.validationError(), "a usable configuration is still loaded")
        XCTAssertEqual(defaults.data(forKey: ProfileStore.storageKey), garbage)
    }

    func testSaveValidatesAndPersists() throws {
        let store = ProfileStore(defaults: defaults)
        var invalid = store.configuration
        invalid.profiles[0].name = " "
        XCTAssertNotNil(store.save(invalid))
        XCTAssertNotEqual(store.configuration, invalid)
        var valid = store.configuration
        valid.profiles[0].name = "Renamed"
        XCTAssertNil(store.save(valid))
        XCTAssertEqual(ProfileStore(defaults: defaults).configuration.profiles[0].name, "Renamed")
    }

    func testBindingCommitsOneEntryAndRefusesUnknownProfiles() {
        let store = ProfileStore(defaults: defaults)
        let other = store.configuration.profiles[1].id
        XCTAssertNil(store.bind(.selection, to: other))
        XCTAssertEqual(ProfileStore(defaults: defaults).configuration.bindings.selectionProfileId, other)
        XCTAssertNotNil(store.bind(.shortcut1, to: "missing-profile"))
        XCTAssertNotEqual(store.configuration.bindings.shortcut1ProfileId, "missing-profile")
    }
}
