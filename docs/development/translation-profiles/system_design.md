# Translation profiles: system design

Planned contracts, not implemented APIs. [Architecture](architecture.md) · [Storage and UI](tech_doc.md) · [Tasks](task_list.md)

## Profile and binding contract

| Field | Meaning |
| --- | --- |
| `id`, `name` | Stable unique reference and user-editable display name. |
| `nativeLanguage`, `foreignLanguage` | Existing ten-language pair. |
| `direction` | `auto`, `nativeToForeign`, or `foreignToNative`. |
| `promptMode` | `preset` or `custom`; only the selected branch executes. |
| `presetStyle`, `systemPrompt` | Existing four-style preset and verbatim custom multiline text; switching modes retains both drafts. |
| `backend`, `model` | BuiltIn, Ollama, or OpenAI-compatible; Ollama/API model explicitly saved, BuiltIn model fixed. |

Schema v2 adds `profiles[]`, `shortcut1ProfileId`, `shortcut2ProfileId`, and `selectionProfileId`. Existing connection endpoints, credentials, enablement, excluded apps, startup, and shortcut configuration remain outside profiles. Profiles/bindings alone govern new translation behavior; no bidirectional sync with legacy global translation fields.

## Direction and target selection

Resolve the physical slot's bound profile before dispatching its action. Native/Foreign actions override profile direction and target that profile's native/foreign language; source remains `Detect(text)`. Smart uses the profile direction and existing language-resolution rules. Its unknown/third-language default target follows the action's popup/replace category, never the physical slot label or preview toggle. A detected third language remains that source language; unresolved source stays `nullopt`.

Temporary target selection changes only the operation's target and rerenders its original template; it does not save profile changes. Explain that literal natural-language targets inside a custom prompt cannot be automatically understood or rewritten.

## Prompt and provider contracts

| Mode/provider | Effective request |
| --- | --- |
| Preset/OpenAI | Existing `translator::SystemPrompt(...)` plus original text as `user`. |
| Preset/Ollama | Existing `PlainPrompt(...)` semantics, unchanged by migration. |
| Custom/OpenAI | Exactly `[system(rendered template), user(original text)]`. |
| Custom/Ollama | Existing `/api/generate`/NDJSON with separate `system=rendered template`, `prompt=original text`; retain `think:false` and existing HTTP 400 retry without it. No `raw:true` or handwritten model token template. |
| BuiltIn | Execute only `preset + faithful`; reject other combinations explicitly. |

Custom replaces the entire system prompt: append no default translation/style/output constraints. Replace only `{{source_language}}` and `{{target_language}}`, case-sensitively, in one pass; never execute code, recursively render, insert selected text into system, or substitute inside user text. Use `lang::PromptName()` for known languages, including third languages; use `auto-detected language` for an unknown source while retaining `nullopt`. Unknown `{{...}}` tokens stay literal with a non-blocking warning.

Reject whitespace-only custom prompts without changing their saved whitespace/Unicode. Use Trim solely to detect an empty response; preserve exact nonblank output in streaming, cache, retry and the popup (display and Copy), including indentation, tabs, and trailing newlines. **Amended by user decision (2026-10-29, final security review):** popup Replace and direct replacement paste that text without trailing line breaks unless the replaced selection itself ended with one (`execution::ReplacementText`), so a pasted line is never also "entered" — a terminal would run it. Indentation, tabs and inner line breaks are kept.

**Remote plain-HTTP Ollama (user decision, 2026-10-29):** still allowed, but shown as "network, unencrypted" in Settings and the profile editor, and Replace actions from such a profile always show the popup first (`profilestatus::RequiresPreview`), because the reply could be altered in transit before it would be pasted into the user's app. Ollama over HTTPS and local routes keep the configured preview setting.

## Sessions and replacement

- Create the session generation **before** `Stream()` so synchronous cache callbacks are guarded. Callbacks only post messages using the callback's request ID and captured session, never an ID awaiting return or mutable current state.
- New user translation/cancel invalidates previous execution. Settings save/binding switches affect later triggers. Retry keeps the original snapshot; target changes reuse its original template. Health/discovery use independent validation tokens, not translation generations.
- Bind popup callbacks to their own session. Close reasons are distinct: **Dismiss** cancels that session; **ReplaceCommit** freezes its result/token, closes, and hands off paste without cancelling that token; **Superseded** invalidates only the old session; **AppExit** cancels all. A stale popup closing cannot cancel a new task.
- Guard capture, results, and replacement through `TextReplacer`'s final Ctrl+A/Ctrl+V boundary. Coordinate generation/cancellation and input commit; verify original target. Serialize capture/replace clipboard transactions with one mutex, with no UI-thread wait. Cancellation/failure still restores the clipboard; already-issued paste cannot be promised reversible.

## Cache and asynchronous identity

Build an unambiguous structured/length-safe identity from effective backend, actual endpoint, model or BuiltIn revision, source/target, actual system/user/plain payload, original text, adapter-parameter/retry-policy version, and non-sensitive **connection epoch**. Profile ID alone and bare-delimiter concatenation are insufficient. Encode the `think:false`/400 fallback policy without claiming knowledge of server-default parameters.

Connection/credential changes increment the epoch; old in-flight cache fills remain in their old namespace. Never include raw credentials in keys, logs, or diagnostics.

Discovery, health tests, and preload carry connection/profile versions and discard stale updates. Missing models are visible and never silently substituted, even on migrated profiles. Preload only actually bound, deduplicated models, not the entire library. Connection tests validate an explicit connection/model; profile tests validate full direction/prompt behavior. Both use fixed synthetic text, never selection/clipboard. Diagnostics exclude prompt, user text, free-form profile names, URLs, and secrets.
