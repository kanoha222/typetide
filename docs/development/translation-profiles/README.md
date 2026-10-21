# Translation profiles: implementation gates

**Scope:** Windows only. **Implementation:** 原生事务已在 commit `9c7b0e3` 实证行为 RED（run `39406689216` / job `118574219317`：configure/build SUCCESS，21 failures，job exit 1，无 infra 2），Core/NativeGuard/Windows 基线仍 PASS。主会话已授权本批 Settings/版本化凭据事务实现；当前源码增量及追加回归尚未执行验证，不是已实证 GREEN。设计文档已完成；完整功能与实测覆盖率仍未验证。当前状态见[环境状态](environment_status.md)。

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
- [Actual native transaction RED](https://github.com/kanoha222/typetide/actions/runs/39406689216/job/118574219317) authorized the current **implemented, not-yet-verified** increment: complete nonrecursive committed ledger/snapshot, Core-backed v2+global metadata persistence, instance commit, safe mutable/const saves, versioned credential staging/rollback/known-origin cleanup and typed Found/NotFound/Error reads. The final ref and full JSON byte bound precede all credential staging/temp I/O; file success publishes once, cleanup failure reports a warning without undoing it. Snapshots resolve keys **in memory only**, never from worker credential reads. No cross-subsystem power-loss atomicity claim or startup enumeration is made.
- The transaction executable still links only the in-memory `TestCredentialFacade.cpp` and scoped fake store, never production `Credentials.cpp`; the storage guard retains its strict zero-unexpected-call double. Production isolated mode without injection uses a directory-scoped process-local credential store, preserving old Settings roundtrips without touching real CM or writing keys into v2 JSON. Existing assertions remain; 14 added edge cases have no independently observed RED/GREEN yet. No local configure/build/run, tool/model download, live credentials or CI polling occurred. Full feature GREEN, Settings/UI/provider integration, full baseline/mock/UIA and measured coverage remain unverified; macOS final status remains unconfirmed.
- Auto-fix is enabled for draft PR #2; no merge or release is authorized. No private machine diagnostics, keys or user text may be published. Versions, settings and unrelated documentation remain unchanged.
