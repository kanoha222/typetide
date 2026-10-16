# Translation profiles: implementation gates

**Scope:** Windows only. **Implementation:** pending actual behavioral RED. 设计文档已完成；声明/测试与独立 CI 入口已准备，仅有静态审查，没有 C++ 编译、实际 RED/GREEN 或覆盖率证据。当前状态见[环境状态](environment_status.md)。

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

- Phase 0 documentation is complete. All 24 implementation/verification checklist items remain pending.
- The [standalone Windows test project](../../../windows/tests/CMakeLists.txt) and [profile-core workflow](../../../.github/workflows/translation-profiles.yml) are test infrastructure only; they do not change the production build or implement profile behavior.
- The user selected GitHub Windows CI with its preinstalled MSVC, Windows SDK and CMake. This bootstrap installs no tools and uses no real network providers, model execution, credentials or selected user text.
- The workflow is authored locally and **has not run**. Actual evidence awaits coordinated publication of the independently reviewed branch/draft PR. Strict-JSON assertion failures are expected, not yet observed; unavailable tools or failed compilation are not behavioral RED.
- Production implementation must wait for a successfully built test executable to demonstrate the intended behavioral failures. GREEN, full baseline/mock/UIA checks and measured coverage follow separately; this small workflow does not replace them.
- The coordinator handles the authorized scoped commit/push/draft PR after code/security review. There is no automatic merge or release, and no private machine diagnostics, keys or user text may be published. Versions, settings and unrelated documentation remain unchanged.
