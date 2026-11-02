import SwiftUI
import AppKit

/// 菜单栏视图：开关、快捷键提示、设置入口。
struct MenuBarView: View {
    @StateObject private var updateChecker = UpdateChecker.shared
    @AppStorage(AppSettings.Keys.enabled) private var isEnabled = true
    @ObservedObject private var profileStore = ProfileStore.shared

    var body: some View {
        if updateChecker.hasNewVersion {
            Button {
                updateChecker.openReleasesPage()
            } label: {
                Label("New update available", systemImage: "arrow.down.circle.fill")
            }
            Divider()
        }

        Toggle(isOn: $isEnabled) {
            Text(isEnabled ? "TypeTide is On" : "TypeTide is Off")
        }
        .onChange(of: isEnabled) { _, _ in
            TriggerController.shared.applyEnabledState()
        }

        Divider()

        Text("\(AppSettings.readShortcutAction.displayName):  \(AppSettings.readShortcut?.displayString ?? "Not set")")
        Text("\(AppSettings.rewriteShortcutAction.displayName):  \(AppSettings.rewriteShortcut?.displayString ?? "Not set")")

        Divider()

        // One submenu per entry: the committed profiles, the bound one checked. A change only
        // affects later triggers.
        ForEach(ProfileEntry.allCases) { entry in
            Menu("\(entry.label) profile") {
                ForEach(profileStore.configuration.profiles) { profile in
                    Button {
                        profileStore.bind(entry, to: profile.id)
                    } label: {
                        if profileStore.configuration.bindings.profileId(for: entry) == profile.id {
                            Label(profile.name, systemImage: "checkmark")
                        } else {
                            Text(profile.name)
                        }
                    }
                }
            }
        }

        Divider()

        // 不用 SettingsLink：accessory App 的菜单栏场景里它可能不激活 App，
        // 设置窗口开在其他 App 后面或根本不出现（#3）。
        Button {
            SettingsOpener.open()
        } label: {
            Label("Settings…", systemImage: "gearshape")
        }
        .keyboardShortcut(",", modifiers: .command)

        Button(role: .destructive) {
            NSApp.terminate(nil)
        } label: {
            Label("Quit TypeTide", systemImage: "power")
        }
        .keyboardShortcut("q", modifiers: .command)
    }
}
