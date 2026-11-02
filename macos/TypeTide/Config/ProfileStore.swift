//
//  ProfileStore.swift
//  TypeTide
//
//  Persists the translation profile configuration as JSON in UserDefaults. The first load
//  migrates the settings of builds without profiles; unreadable saved data is reported and never
//  overwritten until the user saves.
//

import Foundation

@MainActor
final class ProfileStore: ObservableObject {
    static let shared = ProfileStore(defaults: .standard)
    static let storageKey = "translationProfiles"

    @Published private(set) var configuration: ProfileConfiguration
    private(set) var loadError: String?
    private let defaults: UserDefaults

    init(defaults: UserDefaults) {
        self.defaults = defaults
        configuration = .defaults  // RED scaffold
    }

    /// Validates and saves `candidate`; returns the reason when it is refused (nothing changes).
    @discardableResult
    func save(_ candidate: ProfileConfiguration) -> String? {
        nil  // RED scaffold
    }

    /// Binds `entry` to an existing profile; returns the reason when it is refused.
    @discardableResult
    func bind(_ entry: ProfileEntry, to profileId: String) -> String? {
        nil  // RED scaffold
    }
}
