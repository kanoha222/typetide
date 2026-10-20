# Translation profiles: implementation gates

**Scope:** Windows only. **Implementation:** 已验证基线仍为 commit `f724b4c`：原生存储写保护 5/5 check-runs GREEN（run 39364511799，job 118438131020，0 failures）。当前增量仅为原生事务 RED 脚手架修订，尚未 configure/build/run，也尚无事务行为 RED 实证；`commit()` 明确返回 `false` / `NotReady`，完整事务实现必须等待真实云端 RED。设计文档已完成；完整功能与实测覆盖率仍未验证。当前状态见[环境状态](environment_status.md)。

This directory records the approved Windows translation-profile design. macOS adaptation is deferred while retaining the same intended configuration semantics. Follow the repository [agent guide](../../../AGENTS.md).

## Reading map

| Document | Owns |
| --- | --- |
| [PRD](PRD.md) | User outcomes, scope, non-goals, acceptance gates |
| [Architecture](architecture.md) | Module boundaries, reuse, immutable execution flow |
| [System design](system_design.md) | Profile contract, direction, prompts, sessions, cache identity |
| [Technical plan](tech_doc.md) | Migration, strict storage boundary, native editor, test strategy |
| [Task list](task_list.md) | Ordered RED → GREEN work and evidence checklist |
| [Environment status](environment_status.md) | 当前证据、Windows CI 路径与恢复门槛 |

Rules live in their owning document rather than being repeated throughout the set. All proposed APIs/modules are future work unless explicitly identified as existing reuse points.

## Current work boundary

- Phase 0 documentation is complete. Standalone Windows configure/build/run is verified; all Phase 1–5 implementation/verification checklist items remain pending.
- The [standalone Windows test project](../../../windows/tests/CMakeLists.txt) and [profile-core workflow](../../../.github/workflows/translation-profiles.yml) provide isolated test infrastructure, configured with `PROFILE_CORE_JSON_ONLY=OFF` to run the full profile test suite.
- The user selected GitHub Windows CI with its preinstalled MSVC, Windows SDK and CMake. The local SDK route was abandoned after an integrity mismatch, not retained as an active blocker. This bootstrap installs no tools and uses no real network providers, model execution, credentials or selected user text.
- Foundation [PR #1](https://github.com/kanoha222/typetide/pull/1) is merged. Current [draft PR #2](https://github.com/kanoha222/typetide/pull/2) uses `feat/translation-profiles-implementation`. JSON-only GREEN was confirmed at commit `439b9ff`. The full profile/store contract 112 failures behavioral RED was confirmed at commit [708a593](https://github.com/kanoha222/typetide/commit/708a593) ([job 117972461843](https://github.com/kanoha222/typetide/actions/runs/39226040932/job/117972461843)).
- Current work is a **not-yet-run RED scaffold**, not transaction implementation. The transaction executable links only the in-memory `TestCredentialFacade.cpp` and scoped fake store, never production `Credentials.cpp`; the storage-guard executable retains its strict unexpected-call double. New production reference/migration helpers dispatch only to an injected backend or fail closed; the old production legacy credential facade and `save()`/`load()` guards are preserved. The snapshot DTO includes a resolved key **in memory only**; committed-state snapshot behavior is still unimplemented. Full feature GREEN, profile persistence integration, Settings/UI/provider checks, full baseline/mock/UIA and measured coverage remain unverified. Existing Windows app self-test passed at the recorded baseline; macOS final status remains unconfirmed.
- Auto-fix is enabled for draft PR #2; no merge or release is authorized. No private machine diagnostics, keys or user text may be published. Versions, settings and unrelated documentation remain unchanged.
