# Translation profiles: task list

**当前状态：完整 Profile 测试集 112 failures 行为 RED 已实证（commit 708a593）；领域模型与存储编解码已实施，完整 GREEN 待云端复跑验证。** 基础设施 PR #1 已合并；当前 [draft PR #2](https://github.com/kanoha222/typetide/pull/2) 位于 `feat/translation-profiles-implementation`。Windows 独立测试实证 112 failure(s) 行为 RED；现已完成 TranslationProfile.cpp / TranslationProfileStore.cpp 完整领域逻辑实施（Phase 1c）。GREEN、Settings/UI/provider 集成与实测覆盖率仍未验证，Phase 1–5 整项任务待办。draft PR #2 已启用 auto-fix，不合并、不发版。详见[环境状态](environment_status.md)；文档地图见[索引](README.md)。

## Phase 0 — documentation gate

- [x] Read root AGENTS.md and the approved translation-profile plan.
- [x] Document product scope in [PRD](PRD.md).
- [x] Document boundaries/reuse in [architecture](architecture.md).
- [x] Document execution, roles, sessions, and cache epoch in [system design](system_design.md).
- [x] Document migration, strict storage, native editor, and evidence in [technical plan](tech_doc.md).
- [x] Create this ordered checklist with every implementation step still pending.

Documentation must finish before coding. Toolchain and runtime baseline confirmation below are **not** completed by this gate.

## Pre-code prerequisite — environment and baseline

- [x] Verify the GitHub Windows runner's preinstalled MSVC, Windows SDK and CMake with actual standalone configure/build/run evidence ([JSON-only RED job](https://github.com/kanoha222/typetide/actions/runs/39193075553/job/117869875089)). Coverage tooling remains unverified; do not install or download tools in this bootstrap.
- [ ] Record the complete existing build/self-test/mock/UIA baseline and remaining verification gaps. Keep versions/releases/settings unchanged outside the approved implementation.
  - Existing Windows app self-test passed; macOS was still pending when last read, with no final pass confirmed. Full baseline/mock/UIA/coverage evidence remains pending. The local SDK route was abandoned after an integrity mismatch; cloud Windows is the active route, not an unresolved local-install blocker.

## Phase 1 — RED → GREEN: profile model and migration

- [ ] RED: add field/enum/ID/reference and size-boundary failures, schema number checks, full-consumption/depth/string/Unicode failures, multiline/emoji roundtrips, storage-state save blocking, and atomic-save failure cases.
  - JSON-only subset: [Windows JSON 阶段实证 GREEN](https://github.com/kanoha222/typetide/actions/runs/39221251003/job/117956815530)，commit 439b9ff。[完整 profile 测试集实证行为 RED](https://github.com/kanoha222/typetide/actions/runs/39226040932/job/117972461843)，commit 708a593，112 failure(s)。现已完成 Phase 1c 领域校验、能力判定、Defaults/Find/BoundProfile 与 v2/legacy 存储编解码完整实现，完整 GREEN 待云端复跑验证。
- [ ] RED: prove migration binds each slot by action (including swapped/same actions), selection binds read, credential handling stays separate, and unsupported-but-valid profiles survive unchanged.
- [ ] GREEN: implement decoupled types/profile/store with existing JSON APIs, unified limits, candidate validation, atomic persistence/publication, and all-path storage guards.
- [ ] Record failing/passing evidence; refactor without changing legacy behavior.

## Phase 2 — RED → GREEN: prompts, model routing, cache

- [ ] RED: cover full custom override, one-pass/case-sensitive/unknown variables, unknown/third-language sources, all direction rules, preset compatibility, and exact OpenAI/Ollama role/payload/model contracts including `think` fallback.
- [ ] RED: cover BuiltIn capability refusal, blank-only result failure, exact output whitespace, cache payload/model/endpoint isolation, and old-request isolation after a connection/credential epoch change.
- [ ] GREEN: implement immutable effective requests, role-preserving adapters and epoch-aware unambiguous cache identity; retain existing streaming and compatibility guards.
- [ ] Verify unit and two-provider mock evidence, keeping credentials and content out of logs.

## Phase 3 — RED → GREEN: entry bindings and async lifecycle

- [ ] RED: prove physical-slot binding for same/swapped actions, selection binding, disabled shortcuts, and direction independence from slot labels/preview toggles.
- [ ] RED: use barriers for delayed capture, synchronous cache hit, retry/retarget snapshots, stale callbacks and popup close reasons, credential-change backfill, and delayed input-commit cancellation with clipboard restoration.
- [ ] GREEN: integrate main-thread snapshots and captured-session callbacks through App/Popup/Tray and final TextReplacer side effects; serialize clipboard capture/replace without blocking the UI.
- [ ] Verify three tray binding submenus, future-trigger-only profile changes, independent validation tokens, and no stale-window cancellation of a new task.

## Phase 4 — RED → GREEN: native editor and validation

- [ ] RED: write isolated UIA cases before editor implementation for CRUD/restart, drafts/save failure/cancel/dirty switching, preserved preset/custom drafts, Unicode and oversize paste, explicit replacement on deletion, and last-profile/broken-reference handling.
- [ ] RED: cover versioned discovery/health/profile tests, missing-model non-substitution, bound/deduplicated preload, and synthetic-text-only tests.
- [ ] GREEN: implement the independent single-instance editor, accessible multiline control, preview/help, three bindings, and atomic save/publication. Replace competing legacy effective editors with summaries/manager links; retain connection controls and remote-use labels.
- [ ] Verify model/secret isolation and no production credential/selection use in fixtures.

## Phase 5 — verification, review, and evidence

- [ ] Run complete old/new `--selftest`, both provider mocks, and existing plus new Win32/WPF UIA E2E in isolated `TYPETIDE_DATA_DIR`; clean up fixtures/processes.
- [ ] Compare exact custom output across streaming/cache/retry/preview Replace/direct replacement; verify all deterministic race and migration/storage tests.
- [ ] Export real Debug + PDB Cobertura coverage with documented OpenCppCoverage arguments; validate the exported statistics. Reach ≥80% changed/new core line coverage, or report the shortfall/blocker without marking this item done.
- [ ] Report full-project baseline and UI coverage separately; list every unexecuted check and environment limitation.
- [ ] Complete code-reviewer-role and security review, resolve high/critical findings, rerun affected checks, and fill evidence/status back into these documents.
- [ ] Accept only when product, migration, session identity, local/cloud clarity, privacy, tests, and measured coverage gates are met. No automatic commit, push, or release.
