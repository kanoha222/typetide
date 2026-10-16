# Translation profiles: architecture

Planned Windows design; implementation pending. [Product boundary](PRD.md) · [Execution contracts](system_design.md) · [Persistence/UI details](tech_doc.md)

## Data flow

```text
Physical shortcut slot / selection binding
  → main-thread profile + connection + credential snapshot
  → existing selection capture (including clipboard fallback)
  → direction + effective provider request + cache identity
  → existing provider streaming
  → session-bound popup / guarded replacement commit
```

The physical shortcut slot is resolved while `WM_HOTKEY` can still distinguish it. The action controls popup/replace semantics; a slot label or preview option must not change direction resolution.

## Proposed boundaries

Names may follow repository conventions; responsibilities must remain separate.

| Module | Responsibility |
| --- | --- |
| `TranslationTypes.h` / `TranslationProfile.*` | Backend/domain types independent of Settings; immutable profile values, field validation, direction and template logic; avoid circular includes. |
| `TranslationProfileStore.*` | Schema codec, legacy conversion, binding validation, complete candidate validation and persistence using existing JSON facilities. |
| `TranslationExecution.*` | Immutable execution snapshots, effective provider payloads, unambiguous cache identity; pure logic directly accessible to self-tests. |
| `TranslationProfilesWindow.*` | Single-instance native editor, draft interactions, bindings, capability feedback, fixed-text tests; split by responsibility instead of creating another oversized window file. |

## Existing integration/reuse points

- `Settings.*`: retain existing connection/security/app controls and atomic tmp+rename storage; integrate candidate publication and persistent storage-state guards.
- `Language.*`: reuse the ten languages, detection, `lang::ResolveDirection(...)`, and `lang::PromptName()`.
- `Translator.*`: retain `SystemPrompt`, legacy `PlainPrompt`, `Stream`, `Cancel`, WinHTTP, OpenAI SSE, and Ollama NDJSON/`think:false` compatibility; add effective-request and epoch-aware identity.
- `App.*`, `PopupWindow.*`, `TextReplacer.*`: resolve bindings, propagate captured sessions, and guard replacement through the final input side effect.
- `TrayIcon.*`, `SettingsWindow.*`: expose three binding submenus, summaries, connection controls, and a manager entry; remove competing effective language/style/model editors.
- `OllamaModels.*`: version discovery/health/preload work; never auto-replace a saved profile model. Coordinate clipboard/capture at the existing required transaction points.
- Register future sources in Windows CMake only. Existing SettingsWindow helpers are file-private, not assumed reusable APIs.

## Ownership and publication

All UI and configuration reads occur on the main thread. Workers receive immutable profile, connection/model, credentials, and non-sensitive connection-version values; they never read `Settings::shared()`. The snapshot survives delayed capture, streaming, retry, and temporary retargeting.

Use **draft → validated candidate → atomic save → publish complete snapshot**. Do not mutate shared profiles/vectors in place or publish before durable success. Saving or changing a binding does not rewrite an active session. Credentials remain in Credential Manager and never become profile fields, cache keys, or diagnostic values.

Translation sessions and validation operations have separate lifecycles. Session guards, popup close reasons, clipboard serialization, and connection epochs are mandatory correctness boundaries, specified in [system design](system_design.md#sessions-and-replacement).

## Constraints

Keep the native C++20/Win32 architecture and zero additional runtime libraries. Reuse `json::Object`, `json::Array`, `Value::dump()`, and `json::Parse()`; do not invent mutable `Value::push_back()` support or a second settings framework. Keep new files below 800 lines and functions short; no platform, release, or version churn.
