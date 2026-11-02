//
//  ProfileStore.swift
//  TypeTide
//
//  Persists the translation profile configuration as JSON in UserDefaults. The first load
//  migrates the settings of builds without profiles; unreadable saved data is reported and never
//  overwritten until the user saves.
//

import Combine
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
        let migrated = ProfileConfiguration.migrated(from: .read(from: defaults))
        if let data = defaults.data(forKey: Self.storageKey) {
            if let decoded = try? JSONDecoder().decode(ProfileConfiguration.self, from: data),
               decoded.validationError() == nil {
                configuration = decoded
            } else {
                // Keep the unreadable data untouched; run on the migrated profiles until a save.
                configuration = migrated
                loadError = "Your saved translation profiles could not be read. TypeTide is using profiles "
                    + "made from your earlier settings; saving here replaces the unreadable data."
            }
        } else {
            configuration = migrated
            if let data = try? JSONEncoder().encode(migrated) { defaults.set(data, forKey: Self.storageKey) }
        }
    }

    /// Validates and saves `candidate`; returns the reason when it is refused (nothing changes).
    @discardableResult
    func save(_ candidate: ProfileConfiguration) -> String? {
        if let error = candidate.validationError() { return error }
        guard let data = try? JSONEncoder().encode(candidate) else { return "The translation profiles could not be encoded." }
        defaults.set(data, forKey: Self.storageKey)
        configuration = candidate
        loadError = nil
        return nil
    }

    /// Binds `entry` to an existing profile; returns the reason when it is refused.
    @discardableResult
    func bind(_ entry: ProfileEntry, to profileId: String) -> String? {
        guard configuration.profiles.contains(where: { $0.id == profileId }) else {
            return "The selected profile no longer exists."
        }
        return save(configuration.withBinding(entry, to: profileId))
    }
}
