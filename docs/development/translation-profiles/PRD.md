# Translation profiles: product requirements

Status: approved scope documented; implementation pending. See the [index](README.md) for ownership and the [task list](task_list.md) for delivery status.

## Goal

Let Windows users save and select named language × prompt × backend/model combinations while keeping the existing selection capture, streaming popup, and inline replacement workflows. macOS implementation is not in this release.

## Required behavior

| Area | Requirement |
| --- | --- |
| Saved profiles | Create, duplicate, rename, edit, delete, and persist multiple profiles. Stable IDs survive renaming. Each profile selects its language pair, automatic/fixed direction, preset/custom prompt, backend, and model. |
| Entry bindings | Bind each of the two existing shortcut slots independently; bind selection-icon/automatic translation through a third, independent default. Retain the six smart/native/foreign × popup/replace actions and existing preview/direct-replacement behavior. |
| Access beyond two profiles | Tray submenus switch the profile for each of the three bindings; do not add unlimited global shortcuts. |
| Prompt control | Preserve all four legacy style presets. Custom system prompts fully replace default instructions: no hidden translation, faithful-style, or output-only additions. See [request rules](system_design.md#prompt-and-provider-contracts). |
| Backend choice | Save the model per profile. Reuse one existing connection/credential configuration per backend class; this is not a multi-account or multi-endpoint connection manager. Never silently switch model, backend, or local/cloud route. |
| Capability honesty | BuiltIn supports only preset + faithful. Preserve structurally valid but unsupported choices, show why unavailable, and reject execution rather than pretending custom instructions work. |
| Editing safety | Use a separate native editor with drafts, validation, save/cancel, and synthetic-text testing. Cancel leaves live settings unchanged. Deleting a bound profile requires an explicit replacement; the final profile cannot be deleted. Broken references require visible repair. |
| Privacy and routing | Clearly label remote transmission. Keep credentials in the existing secure store; do not log selected text, prompts, arbitrary profile names, URLs, or secrets. |

## Compatibility promises

- Migrate legacy shortcuts according to their **action's popup/replace behavior**, not their physical slot's name; selection uses the migrated read profile.
- Profiles plus bindings become the sole source of translation behavior. Legacy global fields must not remain a second editable authority.
- Saving/switching settings affects future operations only. Retry and temporary target changes retain the originating operation's configuration; stale work cannot update a new session or paste into its target.
- Preserve custom input and output whitespace, Unicode, and line breaks; reject blank-only prompts/results and size overflows visibly rather than truncating.

## Non-goals

No macOS implementation, Win32/HTTP/UI Automation/clipboard framework rewrite, new AI protocol, script-template engine, multi-turn chat, import/export, cloud sync, new languages, training, or repurposing TranslateGemma as a general instruction model. No added runtime dependency.

## Acceptance gates

Functionality and old-action migration must pass the [test matrix](tech_doc.md#verification-and-evidence), including guarded execution, cache identity, role separation, storage preservation, and native editor flows. Require explicit local/cloud routing and no new sensitive logging. Report unexecuted work and environment blockers. Changed/new core logic targets ≥80% measured line coverage; project baseline and UI coverage are separate disclosures, never inferred from test pass counts.
