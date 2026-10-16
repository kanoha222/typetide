# Translation profiles: task list

**当前阻塞：等待云端实际行为 RED，业务实现前暂停。** 设计文档已完成；声明/测试文件与独立 Windows CI 入口已准备，仅经过静态审查。用户已选择使用 GitHub Windows runner 的预装工具链；工作流尚未发布运行，没有 C++ 编译、实际 RED/GREEN 或覆盖率证据。严格 JSON 断言预期失败不等于已观察到失败；缺少工具链、配置或编译失败均**不是行为 RED**。详见[环境状态](environment_status.md)。以下 24 项实现/验证任务全部保持待办；文档地图见[索引](README.md)。

## Phase 0 — documentation gate

- [x] Read root AGENTS.md and the approved translation-profile plan.
- [x] Document product scope in [PRD](PRD.md).
- [x] Document boundaries/reuse in [architecture](architecture.md).
- [x] Document execution, roles, sessions, and cache epoch in [system design](system_design.md).
- [x] Document migration, strict storage, native editor, and evidence in [technical plan](tech_doc.md).
- [x] Create this ordered checklist with every implementation step still pending.

Documentation must finish before coding. Toolchain and runtime baseline confirmation below are **not** completed by this gate.

## Pre-code prerequisite — environment and baseline

- [ ] Verify the GitHub Windows runner's preinstalled MSVC, Windows SDK and CMake with actual standalone configure/build/run evidence. Coverage tooling remains unverified; do not install or download tools in this bootstrap.
- [ ] Record existing build/self-test/mock/UIA baseline and concrete environment blockers. Keep versions/releases/settings unchanged outside the approved implementation.

## Phase 1 — RED → GREEN: profile model and migration

- [ ] RED: add field/enum/ID/reference and size-boundary failures, schema number checks, full-consumption/depth/string/Unicode failures, multiline/emoji roundtrips, storage-state save blocking, and atomic-save failure cases.
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
