//
//  OllamaModelResolver.swift
//  TypeTide
//
//  默认模型自动探测：若配置的模型未安装，挑一个已安装的非 embedding 模型。
//

import Foundation
import Ollama

enum OllamaModelResolver {
    struct ModelRecommendation: Equatable {
        let model: String
        let maximumParameterBillions: Double
    }

    /// 列出已安装模型名。
    static func installedModels() async -> [String] {
        guard let url = URL(string: "\(AppSettings.ollamaHost):\(AppSettings.ollamaPort)") else { return [] }
        let client = Ollama.Client(host: url)
        do {
            let response = try await client.listModels()
            return response.models.map { $0.name }
        } catch {
            return []
        }
    }

    /// 看起来像 embedding 模型（不能用于生成翻译）。
    private static func isEmbedding(_ name: String) -> Bool {
        let n = name.lowercased()
        return n.contains("embed") || n.contains("bge") || n.contains("minilm")
    }

    /// Pick the largest installed model that should fit comfortably in unified memory.
    /// Model tags without an explicit parameter count are ignored unless no sized model exists.
    static func recommendation(
        from models: [String],
        physicalMemoryBytes: UInt64 = ProcessInfo.processInfo.physicalMemory
    ) -> ModelRecommendation? {
        guard !models.isEmpty else { return nil }
        let gib = Double(physicalMemoryBytes) / 1_073_741_824
        let cap: Double = gib < 8 ? 3 : gib < 16 ? 7 : gib < 32 ? 14 : 32
        let sized = models.compactMap { model -> (String, Double)? in
            guard let size = parameterBillions(in: model) else { return nil }
            return (model, size)
        }
        let fitting = sized.filter { $0.1 <= cap }.max { $0.1 < $1.1 }
        let fallback = sized.min { $0.1 < $1.1 }
        guard let selected = fitting ?? fallback else {
            return ModelRecommendation(model: models[0], maximumParameterBillions: cap)
        }
        return ModelRecommendation(model: selected.0, maximumParameterBillions: cap)
    }

    private static func parameterBillions(in model: String) -> Double? {
        let pattern = #"(?i)(?:^|[:_-])(\d+(?:\.\d+)?)b(?:$|[:_-])"#
        guard let regex = try? NSRegularExpression(pattern: pattern),
              let match = regex.firstMatch(in: model, range: NSRange(model.startIndex..., in: model)),
              let range = Range(match.range(at: 1), in: model) else { return nil }
        return Double(model[range])
    }

    /// Warms the Ollama models that bound profiles use and that are installed, each once. A
    /// missing model is only flagged in Settings, never replaced by another one.
    static func preloadBoundModels() async {
        let configuration = ProfileStore.shared.configuration
        var models: [String] = []
        for entry in ProfileEntry.allCases {
            guard let profile = configuration.profile(for: entry), profile.backend == .ollama,
                  !models.contains(profile.model) else { continue }
            models.append(profile.model)
        }
        guard !models.isEmpty else { return }
        let installed = await installedModels()
        for model in models where installed.contains(model) || installed.contains(model + ":latest") {
            await preload(model)
        }
    }

    /// Ask Ollama to load the configured model and keep it warm. This uses an empty,
    /// synthetic prompt and never reads user content.
    static func preload(_ model: String) async {
        guard let url = URL(string: "\(AppSettings.ollamaHost):\(AppSettings.ollamaPort)/api/generate") else { return }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.timeoutInterval = 90
        request.httpBody = try? JSONSerialization.data(withJSONObject: [
            "model": model,
            "prompt": "",
            "stream": false,
            "keep_alive": "10m"
        ])
        _ = try? await URLSession.shared.data(for: request)
    }
}
