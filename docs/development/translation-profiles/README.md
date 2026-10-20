# Translation profiles: implementation gates

**Scope:** Windows only. **Implementation:** 纯核心编解码与领域校验在 commit df2fc7c 达成完整 GREEN（4 项 CI 检查全部成功）；现已完成原生设置存储黑盒保护用例与凭证双重隔离测试脚手架登记，原生存储 RED 待云端复跑验证。设计文档已完成；完整翻译配置功能与覆盖率仍未验证。当前状态见[环境状态](environment_status.md)。

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
- Current work: Phase 1c domain logic and storage codec implementation is completed in `TranslationProfile.cpp` and `TranslationProfileStore.cpp` and confirmed GREEN in cloud CI (commit `df2fc7c`, run 39356274299). Native settings storage test suite is registered (`NativeStorageSelfTest.cpp` and CI job `native-storage`); cloud verification of native storage RED is pending. Full feature GREEN, Settings/UI/provider checks, full baseline/mock/UIA and measured coverage remain unverified. Existing Windows app self-test passed; macOS was still pending when last read, with no final pass confirmed.
- Auto-fix is enabled for draft PR #2; no merge or release is authorized. No private machine diagnostics, keys or user text may be published. Versions, settings and unrelated documentation remain unchanged.
