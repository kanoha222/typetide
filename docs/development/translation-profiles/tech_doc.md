# Translation profiles: technical plan

Implementation pending. [Runtime contracts](system_design.md) · [Architecture](architecture.md) · [Ordered checklist](task_list.md)

## Migration and strict storage boundary

1. For unversioned legacy settings, generate `默认翻译` and `默认改写` profiles from original read/rewrite language, direction, style, backend, and model settings. Bind **each shortcut by its saved action's popup/replace category**, not by slot name; selection binds `默认翻译`. Preserve valid but unsupported choices as unavailable rather than silently correcting them.
2. Schema v2 uses the [profile/binding contract](system_design.md#profile-and-binding-contract). Reuse existing JSON objects/arrays, parser/dump, tmp+rename, and existing Credential Manager migration; never duplicate keys into profiles.
3. Parse into a candidate; validate the complete candidate before commit. Validate `schemaVersion` numeric type, integrality, and range **before conversion**. A persistent unsupported/corrupt storage state must block **every** indirect save path, including tray toggles and first-run writes; returning early from load is not sufficient protection against overwriting the original file.
4. Fully serialize and check actual UTF-8 bytes before Credential Manager or temporary-file writes. Publish a complete new live snapshot only after successful atomic persistence. Failure preserves the old file and live configuration. Structural invalidity blocks saving; structural validity and backend capability are separate checks.

### Shared limits and parser checks

| Boundary | Approved rule |
| --- | --- |
| Profiles | Maximum 64; unique stable IDs, validated ID length and all binding references. |
| Name | Maximum 80 UTF-16 code units; no control characters; escape `&` when shown in tray menus. |
| Custom prompt | Maximum 32 Ki UTF-16 code units; nonblank; preserve original spaces, line breaks, quotes, and Unicode. |
| Serialized settings | At most existing 1 MiB, measured after dump in actual UTF-8 bytes. |
| Remaining fields | Validate language/backend/direction/prompt/style enums and model length; use unified named limits, not scattered magic values. |
| Parser | Require full input consumption, finite nesting depth, valid strings and Unicode boundaries. Add only required boundary fixes, not a general parser rewrite. |

The approved plan does not specify numeric ID/model length caps or nesting depth; select and test explicit constants during the validation step without presenting them as already approved values. Cover malformed numbers, trailing data, excessive nesting, escape errors, multilingual multiline text, emoji, and roundtrip integrity. Reject overflow visibly; do not silently truncate. EDIT control capacity/overlimit paste and `EN_MAXTEXT` must not allow a truncated prompt to be saved as complete input.

## Native editor and integration

- Add a single-instance `TranslationProfilesWindow` following `SettingsWindow::open(HWND)`, not a Web UI or further oversized SettingsWindow expansion.
- Left: list/add/duplicate/delete. Right: name, language/direction, backend/model, preset/custom selector, multiline prompt, variable help and effective-prompt preview. Bottom: save/cancel, fixed-text test, and all three entry bindings.
- Draft changes are isolated. On dirty selection/window changes offer save/discard/cancel. Save follows complete validation → atomic persistence → live publication. Cancel/close without save cannot alter runtime settings. Bound-profile deletion requires an explicit replacement; disallow deleting the last profile and make damaged bindings visible.
- Use native EDIT with `ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN`, wrapping, vertical scrolling, tab order, and keyboard access; Enter inserts a newline instead of saving. Manage input capacity explicitly.
- Existing Translation/Backend/Shortcuts pages keep connection settings, status summaries, and management entry points, not a competing language/style/model authority. Mark cloud execution clearly. Model lists and fixed-text tests obey version guards and capability rules in [system design](system_design.md#cache-and-asynchronous-identity).

## Verification and evidence

**Baseline is not verified.** The approved plan recorded static inspection only: no build/test run; CMake, Ninja, LLVM, and OpenCppCoverage were absent from that session's PATH, and standard `vswhere` was not found. Installation/availability of MSVC/Build Tools remains unconfirmed. This documentation-only phase does not recheck or resolve that environment. Locate the actual toolchain and record the existing baseline before coding; seek separate permission for installation or downloads.

| Layer | Required evidence before implementation is complete |
| --- | --- |
| Unit/self-tests | Add feature-split tests to existing `RunSelfTests()` / `--selftest`. First show RED, then GREEN. Retain legacy language, style, streaming, and BuiltIn contracts. Cover schema/Unicode/limits/migration-by-action, request roles/variables/direction/capability, cache/epoch and cancellation/empty-response guards. |
| Provider integration | Extend loopback-only mocks for OpenAI model/system/user and Ollama preset/plain versus custom/system/prompt, including `think` HTTP 400 fallback retaining the prompt. Restrict methods/paths/input sizes, use fixed synthetic text and no production credentials, and clean up. |
| UIA E2E | Isolate fixtures with `TYPETIDE_DATA_DIR`; preserve Win32/WPF flows. Cover profile CRUD/restart, two slots with the same action, swapped actions, disabled shortcuts, selection binding, all three tray choices, draft cancellation, oversize paste, explicit deletion replacement, missing models, and stale test/discovery results. |
| Deterministic async | Use controllable delays/barriers, not fixed sleeps: delayed capture; synchronous cache hit; old popup close; Dismiss versus ReplaceCommit; stale health/discovery; old-request cache fill after credential changes; delayed paste rejected after a new generation with clipboard restoration. |
| Exact output | Use replies with leading indentation, tabs, trailing newline; compare streaming/cache/retry/preview Replace/direct replacement byte-for-byte and reject whitespace-only output. Adjust the Translator/App trim points identified in the approved plan without trimming cached values. |
| Coverage/review | Debug + PDB with OpenCppCoverage; use documented flags, a real Cobertura export, and validated statistics. Target ≥80% line coverage for changed/new core logic; separately report project baseline and UI coverage scope. Never substitute test pass percentage, fabricate threshold flags, or mark unexecuted/blocked gates passed. Follow with code-reviewer-role and security review; resolve high/critical findings before acceptance. |

Completion also requires lossless legacy migration, stable execution identity, explicit local/cloud routing, no new sensitive logs, and an honest list of unexecuted items/blockers. This phase produces no runtime or coverage claim.

## Approved prior art and references

The approved plan already completed repository/code and package research: archived GPL-3.0 Pot is interaction reference only (no code copying); MIT `nlohmann-json` was considered but existing JSON is sufficient, so no new runtime library. Exa was unavailable during research; do not claim it ran.

Reference links below are carried from the approved plan, not fetched in this phase: [Ollama generate](https://docs.ollama.com/api/generate), [Win32 EDIT styles](https://learn.microsoft.com/en-us/windows/win32/controls/edit-control-styles), [OpenCppCoverage usage](https://github.com/OpenCppCoverage/OpenCppCoverage/wiki/Usage), [coverage CLI](https://github.com/OpenCppCoverage/OpenCppCoverage/wiki/Command-line-reference).
