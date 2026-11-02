//
//  TriggerController.swift
//  TypeTide
//
//  编排主链路：
//  - 读·划词翻译：快捷键 / 划词图标 / 自动 → 弹窗流式显示译文。
//  - 写·输入改写：快捷键 → 译为目标语言 → 直接替换或先预览。
//  Shortcut 1, Shortcut 2 and the selection trigger each run the translation profile bound to
//  them; the profile is frozen when the trigger fires, so later edits only affect later triggers.
//

import AppKit
import ApplicationServices

@MainActor
final class TriggerController {
    static let shared = TriggerController()
    private init() {}

    static let noProfileMessage = "No translation profile is bound to this shortcut. Choose one in Settings → Profiles."

    private let readHotkeyID: UInt32 = 1
    private let rewriteHotkeyID: UInt32 = 2
    private let maximumInputCharacters = 5_000

    private var streamTask: Task<Void, Never>?
    private var rewriteTask: Task<Void, Never>?
    private var rewriteRequestID: UInt64 = 0

    private(set) var readShortcutStatus: ShortcutRegistrationStatus = .inactive
    private(set) var rewriteShortcutStatus: ShortcutRegistrationStatus = .inactive

    var shortcutsReady: Bool {
        ShortcutConfiguration.isReady(
            read: AppSettings.readShortcut, readStatus: readShortcutStatus,
            rewrite: AppSettings.rewriteShortcut, rewriteStatus: rewriteShortcutStatus
        )
    }

    // MARK: - 生命周期

    func start() {
        applyEnabledState()
    }

    /// 根据开关 + 快捷键 + 划词设置重新装配触发。
    func applyEnabledState() {
        GlobalShortcutCenter.shared.unregisterAll()
        SelectionMonitor.shared.stop()
        SelectionIconWindow.shared.hide()
        readShortcutStatus = .inactive
        rewriteShortcutStatus = .inactive

        guard AppSettings.isEnabled else { return }

        let readShortcut = AppSettings.readShortcut
        let rewriteShortcut = AppSettings.rewriteShortcut
        if ShortcutConfiguration.isDuplicate(readShortcut, rewriteShortcut) {
            readShortcutStatus = .duplicate
            rewriteShortcutStatus = .duplicate
            return
        }

        // The physical slot chooses the profile; its configured action chooses popup or replace.
        if let readShortcut {
            readShortcutStatus = GlobalShortcutCenter.shared.register(id: readHotkeyID, shortcut: readShortcut) { [weak self] in
                Task { @MainActor in self?.performShortcut(.shortcut1, action: AppSettings.readShortcutAction) }
            }
        } else {
            readShortcutStatus = .notConfigured
        }
        if let rewriteShortcut {
            rewriteShortcutStatus = GlobalShortcutCenter.shared.register(id: rewriteHotkeyID, shortcut: rewriteShortcut) { [weak self] in
                Task { @MainActor in self?.performShortcut(.shortcut2, action: AppSettings.rewriteShortcutAction) }
            }
        } else {
            rewriteShortcutStatus = .notConfigured
        }

        setupSelectionTrigger()
    }

    private func setupSelectionTrigger() {
        let mode = AppSettings.selectionTrigger
        guard mode != .none else { return }

        SelectionMonitor.shared.onSelection = { [weak self] text, location, element, range in
            guard let self, AppSettings.isEnabled, AccessibilityPermission.isGranted,
                  !AppSettings.shouldSkipFrontmostApplication() else { return }
            switch mode {
            case .none:
                break
            case .icon:
                // 图标贴合选区：优先用选区屏幕矩形，拿不到再退回鼠标点
                let anchor = PopupPositioner.anchorRect(element: element, range: range)
                SelectionIconWindow.shared.show(near: anchor, fallback: location) {
                    // Clicking the icon is the trigger: the selection profile is frozen now.
                    self.presentRead(text: text, element: element, range: range)
                }
            case .auto:
                self.presentRead(text: text, element: element, range: range)
            }
        }
        SelectionMonitor.shared.start()
    }

    private func boundProfile(_ entry: ProfileEntry) -> TranslationProfile? {
        ProfileStore.shared.configuration.profile(for: entry)
    }

    // MARK: - 读·划词翻译

    private func performShortcut(_ entry: ProfileEntry, action: ShortcutAction) {
        if action.usesPopup { handleRead(entry: entry, action: action) }
        else { handleRewrite(entry: entry, action: action) }
    }

    func handleRead(entry: ProfileEntry = .shortcut1, action: ShortcutAction = .smartPopup) {
        guard AppSettings.isEnabled else { return }
        guard AccessibilityPermission.isGranted else {
            AccessibilityPermission.request()
            return
        }
        guard !AppSettings.shouldSkipFrontmostApplication() else { return }
        guard let profile = boundProfile(entry) else {
            presentCaptureFailure(Self.noProfileMessage)
            return
        }
        Task { @MainActor in
            guard let cap = await SelectionCapture.readSelection() else {
                presentCaptureFailure("Couldn’t read selected text. The app may not expose its selection and the clipboard fallback received no text.")
                return
            }
            guard cap.text.count <= maximumInputCharacters else {
                presentCaptureFailure("The selection is too long (\(cap.text.count) characters; maximum \(maximumInputCharacters)).")
                return
            }
            presentRead(text: cap.text, element: cap.element, range: cap.range, profile: profile, action: action)
        }
    }

    private func presentCaptureFailure(_ message: String) {
        let mouse = NSEvent.mouseLocation
        let anchor = NSRect(x: mouse.x, y: mouse.y, width: 1, height: 1)
        let model = PopupController.shared.show(original: "No selection", target: LanguageConfig.sourceLanguage,
                                                anchor: anchor, onReplace: nil)
        model.failTranslation(message)
    }

    /// 显示读翻译弹窗并流式填充（供快捷键 / 图标 / 自动模式共用）。`profile` nil = the profile bound
    /// to the selection trigger.
    func presentRead(text: String, element: AXUIElement?, range: CFRange?, dismissOnInteraction: Bool = true,
                     profile: TranslationProfile? = nil, action: ShortcutAction = .smartPopup) {
        guard let profile = profile ?? boundProfile(.selection) else {
            presentCaptureFailure(Self.noProfileMessage)
            return
        }
        let anchor = PopupPositioner.anchorRect(element: element, range: range)
        let route = ProfileExecution.route(for: profile, action: action, text: text)
        let model = PopupController.shared.show(original: text, target: route.target, anchor: anchor,
                                                onReplace: nil, dismissOnInteraction: dismissOnInteraction)
        model.onDismiss = { [weak self] in self?.streamTask?.cancel() }
        model.onReplace = { [weak model] in
            guard let model, let delivered = ProfileExecution.deliveredOutput(for: profile, raw: model.translation) else { return }
            let replacement = ProfileExecution.replacementText(delivered, source: text)
            Task { @MainActor in
                PopupController.shared.close()
                await TextReplacer.replace(with: replacement, selectAll: false)
            }
        }
        model.onRetarget = { [weak self, weak model] newTarget in
            guard let self, let model else { return }
            model.targetLanguage = newTarget
            self.runTranslationStream(text: text, profile: profile,
                                      route: TranslationRoute(source: route.source, target: newTarget), into: model)
        }
        runTranslationStream(text: text, profile: profile, route: route, into: model)
    }

    // MARK: - 共用流式

    /// 共用的流式翻译，把结果写入弹窗 model（读 / 改写预览 / 弹窗重定向复用）。
    private func runTranslationStream(text: String, profile: TranslationProfile, route: TranslationRoute,
                                      into model: TranslationPopupModel) {
        streamTask?.cancel()
        model.onRetry = { [weak self, weak model] in
            guard let self, let model else { return }
            self.runTranslationStream(text: text, profile: profile, route: route, into: model)
        }
        model.resetTranslation()
        streamTask = Task { @MainActor in
            do {
                for try await delta in TranslationService.shared.stream(text: text, profile: profile, route: route) {
                    if Task.isCancelled { return }
                    model.appendTranslation(delta)
                }
                model.finishTranslation()
                if model.translation.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                    model.failTranslation("No translation returned")
                } else {
                    try? await Task.sleep(for: .milliseconds(420))
                    if !Task.isCancelled { model.finishTide() }
                }
            } catch {
                model.failTranslation((error as? TranslationError)?.errorDescription ?? error.localizedDescription)
            }
        }
    }

    // MARK: - 写·输入改写

    func handleRewrite(entry: ProfileEntry = .shortcut2, action: ShortcutAction = .smartReplace) {
        guard AppSettings.isEnabled else { return }
        guard AccessibilityPermission.isGranted else {
            AccessibilityPermission.request()
            return
        }
        guard !AppSettings.shouldSkipFrontmostApplication() else { return }
        guard let profile = boundProfile(entry) else {
            presentCaptureFailure(Self.noProfileMessage)
            return
        }
        // An unencrypted network reply is never pasted unseen: Replace then always previews.
        let preview = AppSettings.rewritePreview ||
            ProfileExecution.requiresPreview(profile, ollamaHost: AppSettings.ollamaHost)
        rewriteTask?.cancel()
        rewriteRequestID &+= 1
        let requestID = rewriteRequestID
        rewriteTask = Task { @MainActor [self] in
            guard let cap = await SelectionCapture.captureForRewrite() else {
                guard isCurrentRewrite(requestID) else { return }
                presentCaptureFailure("Couldn’t read text to rewrite. Select text, or place the cursor in an editable text field, then try again.")
                return
            }
            guard isCurrentRewrite(requestID) else { return }
            guard cap.text.count <= maximumInputCharacters else {
                presentCaptureFailure("The text is too long (\(cap.text.count) characters; maximum \(maximumInputCharacters)).")
                return
            }
            let route = ProfileExecution.route(for: profile, action: action, text: cap.text)

            if preview {
                guard isCurrentRewrite(requestID) else { return }
                // 先预览：弹窗显示译文 + Replace 按钮（顶部可改目标语言）
                let anchor = PopupPositioner.anchorRect(element: cap.element, range: cap.selectedRange)
                let model = PopupController.shared.show(original: cap.text, target: route.target, anchor: anchor, onReplace: nil)
                model.onDismiss = { [weak self] in self?.streamTask?.cancel() }
                model.onReplace = { [weak self, weak model] in
                    guard let self, self.isCurrentRewrite(requestID), let model,
                          let delivered = ProfileExecution.deliveredOutput(for: profile, raw: model.translation) else { return }
                    let replacement = ProfileExecution.replacementText(delivered, source: cap.text)
                    Task { @MainActor in
                        guard self.isCurrentRewrite(requestID) else { return }
                        PopupController.shared.close()
                        await TextReplacer.replace(with: replacement, selectAll: cap.isWholeField)
                    }
                }
                model.onRetarget = { [weak self, weak model] newTarget in
                    guard let self, let model else { return }
                    model.targetLanguage = newTarget
                    self.runTranslationStream(text: cap.text, profile: profile,
                                              route: TranslationRoute(source: route.source, target: newTarget), into: model)
                }
                runTranslationStream(text: cap.text, profile: profile, route: route, into: model)
            } else {
                // 直接替换
                do {
                    let translated = try await TranslationService.shared.translateFully(
                        text: cap.text, profile: profile, route: route)
                    guard isCurrentRewrite(requestID) else { return }
                    await TextReplacer.replace(with: ProfileExecution.replacementText(translated, source: cap.text),
                                               selectAll: cap.isWholeField)
                } catch {
                    guard isCurrentRewrite(requestID) else { return }
                    let anchor = PopupPositioner.anchorRect(element: cap.element, range: cap.selectedRange)
                    let model = PopupController.shared.show(original: cap.text, target: route.target, anchor: anchor, onReplace: nil)
                    model.failTranslation((error as? TranslationError)?.errorDescription ?? error.localizedDescription)
                }
            }
        }
    }

    private func isCurrentRewrite(_ requestID: UInt64) -> Bool {
        requestID == rewriteRequestID && !Task.isCancelled
    }
}
