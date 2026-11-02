//
//  BackendHealthChecker.swift
//  TypeTide
//

import Foundation

struct BackendHealthResult: Equatable {
    enum Status: Equatable {
        case success
        case failure
    }

    let status: Status
    let message: String
    let firstTokenMilliseconds: Int?
    let totalMilliseconds: Int

    var isSuccess: Bool { status == .success }
}

@MainActor
enum BackendHealthChecker {
    /// Tests the Shortcut 1 profile's backend and model.
    static func check() async -> BackendHealthResult {
        await check(profile: TranslationService.shared.primaryProfile)
    }

    /// Tests `profile`'s backend + model as a faithful preset translation of fixed synthetic text,
    /// never a selection, the clipboard or the profile's own prompt, and never from the cache.
    static func check(profile: TranslationProfile) async -> BackendHealthResult {
        var probe = profile
        probe.promptMode = .preset
        probe.presetStyle = .faithful
        let start = ContinuousClock.now
        var firstToken: ContinuousClock.Instant?
        do {
            // Fixed synthetic text: health checks never use clipboard or selected content.
            for try await delta in TranslationService.shared.stream(
                text: "TypeTide health check",
                profile: probe,
                route: TranslationRoute(source: .english, target: .chinese),
                useCache: false
            ) {
                if !delta.isEmpty, firstToken == nil { firstToken = .now }
            }
            let total = start.duration(to: .now).milliseconds
            let first = firstToken.map { start.duration(to: $0).milliseconds }
            let result = BackendHealthResult(
                status: .success,
                message: "Connected and translated successfully.",
                firstTokenMilliseconds: first,
                totalMilliseconds: total
            )
            record(result, backend: probe.backend)
            return result
        } catch {
            let result = BackendHealthResult(
                status: .failure,
                message: userFacingMessage(for: error, backend: probe.backend),
                firstTokenMilliseconds: firstToken.map { start.duration(to: $0).milliseconds },
                totalMilliseconds: start.duration(to: .now).milliseconds
            )
            record(result, backend: probe.backend)
            return result
        }
    }

    private static func userFacingMessage(for error: Error, backend: TranslationBackend) -> String {
        if let error = error as? TranslationError {
            return error.errorDescription ?? "The backend test failed."
        }
        if let urlError = error as? URLError {
            switch urlError.code {
            case .timedOut:
                return "The backend timed out. Check the server and network connection."
            case .cannotConnectToHost, .cannotFindHost, .networkConnectionLost, .notConnectedToInternet:
                return backend == .ollama
                    ? "Can't reach Ollama. Start Ollama and verify 127.0.0.1:11434."
                    : "Can't reach the cloud endpoint. Check the Base URL and network connection."
            default:
                return urlError.localizedDescription
            }
        }
        return error.localizedDescription
    }

    private static func record(_ result: BackendHealthResult, backend: TranslationBackend) {
        let event = DiagnosticEvent(
            name: .backendHealthCheck,
            outcome: result.isSuccess ? "success" : "failure",
            backend: backend.rawValue,
            errorCategory: result.isSuccess ? nil : "backend",
            firstTokenMilliseconds: result.firstTokenMilliseconds,
            totalMilliseconds: result.totalMilliseconds
        )
        Task { await LocalDiagnostics.shared.record(event) }
    }
}
