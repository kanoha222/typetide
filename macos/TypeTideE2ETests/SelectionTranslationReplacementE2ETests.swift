import AppKit
import XCTest
@testable import TypeTide

private struct MockStreamingProvider: TranslationProvider {
    let id = "e2e-mock"

    func stream(_ request: TranslationRequest) -> AsyncThrowingStream<String, Error> {
        AsyncThrowingStream { continuation in
            XCTAssertEqual(request.text, "你好世界")
            XCTAssertEqual(request.target, .english)
            continuation.yield("MOCK_")
            continuation.yield("TRANSLATION_OK")
            continuation.finish()
        }
    }
}

/// A custom profile: its rendered system prompt arrives, the text is unchanged, the reply keeps
/// indentation, a tab and a trailing line break.
private struct CustomPromptProvider: TranslationProvider {
    let id = "e2e-custom"

    func stream(_ request: TranslationRequest) -> AsyncThrowingStream<String, Error> {
        AsyncThrowingStream { continuation in
            XCTAssertEqual(request.systemPrompt, "Into English, please.")
            XCTAssertEqual(request.text, "你好世界")
            continuation.yield("  CUSTOM_")
            continuation.yield("OUTPUT\n\tline 2\n")
            continuation.finish()
        }
    }
}

@MainActor
final class SelectionTranslationReplacementE2ETests: XCTestCase {
    override func tearDown() async throws {
        PopupController.shared.close()
        TranslationService.shared.providerOverride = nil
        TextReplacer.replacementOverride = nil
        try await super.tearDown()
    }

    func testSelectedTextStreamsIntoPopupAndReplaceUpdatesEditor() async throws {
        let editor = NSTextView(frame: NSRect(x: 0, y: 0, width: 480, height: 180))
        editor.string = "前缀 你好世界 后缀"
        let selected = (editor.string as NSString).range(of: "你好世界")
        editor.setSelectedRange(selected)
        XCTAssertEqual((editor.string as NSString).substring(with: editor.selectedRange()), "你好世界")

        TranslationService.shared.providerOverride = MockStreamingProvider()
        TextReplacer.replacementOverride = { [weak editor] replacement, selectAll in
            guard let editor else { return }
            if selectAll { editor.selectAll(nil) }
            editor.insertText(replacement, replacementRange: editor.selectedRange())
        }

        // This is the same entry point used after SelectionCapture has obtained
        // a real AX selection. It creates the production NSPanel and streams into it.
        TriggerController.shared.presentRead(
            text: (editor.string as NSString).substring(with: editor.selectedRange()),
            element: nil,
            range: nil,
            dismissOnInteraction: false
        )

        XCTAssertTrue(PopupController.shared.isVisible)
        let model = try XCTUnwrap(PopupController.shared.currentModel)
        try await waitUntil("streamed translation") { model.translation == "MOCK_TRANSLATION_OK" }
        XCTAssertEqual(model.translation, "MOCK_TRANSLATION_OK")
        try await waitUntil("completion animation") { model.tidePhase == .complete }

        model.onReplace?()
        try await waitUntil("editor replacement") { editor.string.contains("MOCK_TRANSLATION_OK") }

        XCTAssertEqual(editor.string, "前缀 MOCK_TRANSLATION_OK 后缀")
        XCTAssertFalse(PopupController.shared.isVisible)
    }

    func testCustomProfilePopupShowsExactOutputAndReplaceDropsTheTrailingLineBreak() async throws {
        let editor = NSTextView(frame: NSRect(x: 0, y: 0, width: 480, height: 180))
        editor.string = "前缀 你好世界 后缀"
        editor.setSelectedRange((editor.string as NSString).range(of: "你好世界"))

        TranslationService.shared.providerOverride = CustomPromptProvider()
        TextReplacer.replacementOverride = { [weak editor] replacement, selectAll in
            guard let editor else { return }
            if selectAll { editor.selectAll(nil) }
            editor.insertText(replacement, replacementRange: editor.selectedRange())
        }
        let custom = TranslationProfile(id: "e2e-custom", name: "E2E custom", nativeLanguage: .chinese,
                                        foreignLanguage: .english, direction: .auto, promptMode: .custom,
                                        presetStyle: .faithful, systemPrompt: "Into {{target_language}}, please.",
                                        backend: .ollama, model: "e2e-model")

        TriggerController.shared.presentRead(text: "你好世界", element: nil, range: nil,
                                             dismissOnInteraction: false, profile: custom)

        let model = try XCTUnwrap(PopupController.shared.currentModel)
        try await waitUntil("exact custom output") { model.translation == "  CUSTOM_OUTPUT\n\tline 2\n" }
        try await waitUntil("completion animation") { model.tidePhase == .complete }

        model.onReplace?()
        try await waitUntil("editor replacement") { editor.string.contains("CUSTOM_OUTPUT") }
        XCTAssertEqual(editor.string, "前缀   CUSTOM_OUTPUT\n\tline 2 后缀",
                       "Replace keeps indentation and the tab but drops the trailing line break")
    }

    private func waitUntil(_ state: String, timeout: Duration = .seconds(5),
                           condition: @escaping @MainActor () -> Bool) async throws {
        let clock = ContinuousClock()
        let deadline = clock.now.advanced(by: timeout)
        while !condition() {
            guard clock.now < deadline else {
                XCTFail("Timed out waiting for end-to-end state: \(state)")
                return
            }
            try await Task.sleep(for: .milliseconds(20))
        }
    }
}
