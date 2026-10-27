# Translation profiles: task list

**当前状态：commit `9c7b0e3` 的原生事务已实证行为 RED（run `39406689216` / job `118574219317`：21 failure(s)），其实现在 commit `60f0e4e` 实证 GREEN（run `39576810968` / [Native job `119144243876`](https://github.com/kanoha222/typetide/actions/runs/39576810968/job/119144243876) success；全部 6 个 check-run success）。Phase 2 的纯函数测试（`TranslationExecutionSelfTest` 及 `translation-execution` job）已在 commit `a117a72` 实证行为 RED（314 failure(s)，exit 1），纯函数实现已在 commit `f819c2b` 实证 GREEN（[job `120275597655`](https://github.com/kanoha222/typetide/actions/runs/39911587455/job/120275597655)，0 failure(s)，全部 7 个 check-run success）。运行时接入已在 commit `43093f7` 实证行为 RED（run `39931004232`：纯函数 38、运行时 mock 17 failure(s)，build 成功），实现在 commit `54f0e9a` [实证 GREEN](https://github.com/kanoha222/typetide/actions/runs/40273382829/job/121410872452)（纯函数 0 failure(s)、运行时 mock 全部通过、8 个 check-run success）；覆盖率仍未验证。** 基础设施 PR #1 已合并；当前 [draft PR #2](https://github.com/kanoha222/typetide/pull/2) 位于 `feat/translation-profiles-implementation`，auto-fix 开启，不合并、不发版。本批包含完整值 ledger/committed snapshot、v2 与 globals codec、安全 mutable/const save、凭据暂存与回滚/来源受限清理、typed read 与隔离内存后端；原断言保留，追加 14 项边界回归未独立运行 RED。Settings/UI/provider 完整集成与实测覆盖率仍未验证，Phase 1–5 整项任务待办。详见[环境状态](environment_status.md)；文档地图见[索引](README.md)。

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
  - Existing Windows app self-test passed; macOS was pending at that historical observation, but [Mac CI at commit `60f0e4e`](https://github.com/kanoha222/typetide/actions/runs/39576811019/job/119144243054) now confirms success; feature-level macOS adaptation remains unimplemented, and full feature acceptance/baseline/mock/UIA/E2E/measured coverage remain pending. The local SDK route was abandoned after an integrity mismatch; cloud Windows is the active route, not an unresolved local-install blocker.

## Phase 1 — RED → GREEN: profile model and migration

- [ ] RED: add field/enum/ID/reference and size-boundary failures, schema number checks, full-consumption/depth/string/Unicode failures, multiline/emoji roundtrips, storage-state save blocking, and atomic-save failure cases.
  - JSON-only subset: [Windows JSON 阶段实证 GREEN](https://github.com/kanoha222/typetide/actions/runs/39221251003/job/117956815530)，commit 439b9ff。[完整 profile 测试集实证行为 RED](https://github.com/kanoha222/typetide/actions/runs/39226040932/job/117972461843)，commit 708a593，112 failure(s)。[最新测试运行实证 1 failure(s)](https://github.com/kanoha222/typetide/actions/runs/39233983588/job/117998489088)，commit d469920。[纯核心完整 GREEN 已经验证](https://github.com/kanoha222/typetide/actions/runs/39356274299/job/118411675231)，commit df2fc7c，0 failures。[原生设置存储实证 10 failure(s) 真实 RED](https://github.com/kanoha222/typetide/actions/runs/39361446003/job/118428324821)，commit 231883a。[原生设置存储写保护实证 5/5 GREEN](https://github.com/kanoha222/typetide/actions/runs/39364511799/job/118438131020)，commit f724b4c。[原生事务实证 21 failure(s) 行为 RED](https://github.com/kanoha222/typetide/actions/runs/39406689216/job/118574219317)，commit `9c7b0e3`，configure/build 成功、无 infra 2。获授权后实现 `SettingsData` 非递归 ledger、profile-aware instance commit/committed snapshot、v2+全 globals codec、CoCreateGuid ref、先完整字节限额再 stage/flush/replace/发布/已知来源清理、typed credential reads、mutable/const save 边界及隔离进程内存 store；原独立 link-time facade 与 strict double 隔离保留。追加 14 项边界回归先书写但未独立观察到 RED；本批已发布为 commit `60f0e4e`，[Native suite 实证 GREEN](https://github.com/kanoha222/typetide/actions/runs/39576810968/job/119144243876) 经 `RunAdditionalCases` 包含这些新增用例，但未测覆盖率、未本地构建/运行或触及真实 CM，亦不代表完整功能验收。
- [ ] RED: prove migration binds each slot by action (including swapped/same actions), selection binds read, credential handling stays separate, and unsupported-but-valid profiles survive unchanged.
- [ ] GREEN: implement decoupled types/profile/store with existing JSON APIs, unified limits, candidate validation, atomic persistence/publication, and all-path storage guards.
- [ ] Record failing/passing evidence; refactor without changing legacy behavior.

## Phase 2 — RED → GREEN: prompts, model routing, cache

- [ ] RED: cover full custom override, one-pass/case-sensitive/unknown variables, unknown/third-language sources, all direction rules, preset compatibility, and exact OpenAI/Ollama role/payload/model contracts including `think` fallback.
  - 测试：`windows/tests/TranslationExecution*Tests.cpp`、目标 `TranslationExecutionSelfTest`、job `translation-execution`。[实证行为 RED](https://github.com/kanoha222/typetide/actions/runs/39910106404/job/120270873137)：commit `a117a72`，configure/build 成功，314 failure(s)，exit 1（首推 `b2327d1` 为测试代码 MSVC 编译错误，已修）。随后 commit `f819c2b` 实现 `TranslationExecution.*`/`EndpointPolicy.*`/`HttpUrl.*` 与 `profiles::IsBlankText`，[实证 GREEN](https://github.com/kanoha222/typetide/actions/runs/39911587455/job/120275597655)（0 failure(s)）。仍未实现：Http.cpp 共享 `CrackUrl` 并禁用重定向、Translator 转发——均属运行时阶段。
- [ ] RED: cover BuiltIn capability refusal, blank-only result failure, exact output whitespace, cache payload/model/endpoint isolation, and old-request isolation after a connection/credential epoch change.
- [ ] GREEN: implement immutable effective requests, role-preserving adapters and epoch-aware unambiguous cache identity; retain existing streaming and compatibility guards.
- [ ] Verify unit and two-provider mock evidence, keeping credentials and content out of logs.

## Phase 3 — RED → GREEN: entry bindings and async lifecycle

- [ ] RED: prove physical-slot binding for same/swapped actions, selection binding, disabled shortcuts, and direction independence from slot labels/preview toggles.
- [ ] RED: use barriers for delayed capture, synchronous cache hit, retry/retarget snapshots, stale callbacks and popup close reasons, credential-change backfill, and delayed input-commit cancellation with clipboard restoration.
  - [实证行为 RED](https://github.com/kanoha222/typetide/actions/runs/40277449813/job/121423092965)：commit `6e7c858`，纯函数 19 failure(s)、运行时 mock [5 failure(s)](https://github.com/kanoha222/typetide/actions/runs/40277449813/job/121423093049)，均 build 成功、exit 1（“请求进行中改连接”一组本就通过，是既有行为的回归保护）。实现于 commit `3de750f` [实证 GREEN](https://github.com/kanoha222/typetide/actions/runs/40444337477/job/121991887732)（纯函数 0 failure(s)、运行时 mock 全部通过、8 个 check-run success），审查修正见环境状态。测试内容：纯函数 `TranslationSessionTests.cpp`（会话代次、每次尝试的 ticket、四种关闭原因、粘贴守卫与目标窗口、`profiles::WithBinding`）进入 `TranslationExecutionSelfTest`；`--selftest-runtime` 新增托盘绑定提交、替换中止后剪贴板不变、请求进行中改连接（`mock-slow`）三组。`session::Ledger`、`WithBinding`、`profilebinding::Commit` 与 `replacer::ReplaceForTesting` 在 RED 时为桩实现。
- [ ] GREEN: integrate main-thread snapshots and captured-session callbacks through App/Popup/Tray and final TextReplacer side effects; serialize clipboard capture/replace without blocking the UI.
- [ ] Verify three tray binding submenus, future-trigger-only profile changes, independent validation tokens, and no stale-window cancellation of a new task.

## Phase 4 — RED → GREEN: native editor and validation

- [ ] RED: write isolated UIA cases before editor implementation for CRUD/restart, drafts/save failure/cancel/dirty switching, preserved preset/custom drafts, Unicode and oversize paste, explicit replacement on deletion, and last-profile/broken-reference handling.
  - 第 1 步（纯函数编辑器模型）：[实证行为 RED](https://github.com/kanoha222/typetide/actions/runs/40447544911/job/122002294917)，commit `d6e3f55`，29 failure(s)，build 成功（首推 `c8d5708` 因测试里无上界的循环在桩实现下卡到 10 分钟超时，已改为有界循环）；实现于 commit `df0b8b3`，在 `209a345` 的 [纯函数 job](https://github.com/kanoha222/typetide/actions/runs/40448037403/job/122003876617) 实证 GREEN（0 failure(s)）。测试内容：`windows/tests/ProfileEditorTests.cpp` 覆盖 draft 隔离与原样存储、preset/custom 草稿互不丢失、切换 backend 保留模型、增/复制/删（绑定的 profile 需显式替换、最后一个不可删）、悬空绑定、名称/提示词长度与空白校验（不截断）、有效提示词预览，以及放弃/保存后基线。第 2 步（原生窗口）：[实证行为 RED](https://github.com/kanoha222/typetide/actions/runs/40448037403/job/122003876635)，commit `209a345`，configure/build 成功，唯一失败为 “the profile editor opens”（窗口仍是桩），exit 1；随后提交原生窗口实现，GREEN 待观察。UIA 测试 `windows/scripts/test-profiles-e2e.ps1`（job `profile-editor-uia`，Windows PowerShell 5.1 + UI Automation，隔离 `TYPETIDE_DATA_DIR`）覆盖新增/编辑/保存与原样提示词、未保存草稿关闭时放弃、重启后保留、超长提示词拒绝保存且不截断、删除已绑定 profile 时选择替换；第 3 步（设置窗口/托盘入口、模型不再静默替换、过期健康检查结果丢弃）待做。
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

## Integration gate — 待办，非已完成功能

- [ ] 公开 `Settings` 字段目前仍是旧版可编辑 draft；翻译执行已改读 committed snapshot（GREEN `54f0e9a`），旧设置窗口的 backend/model/语言/方向/风格控件在 profile 编辑器完成前不影响翻译（连接字段仍有效）。保存失败时旧版行为不变，未提交的 draft 可能继续保留。
- [ ] 功能验收前，UI 必须使用 candidate commit，并在提交后重新同步控件、处理 error + warning / 只读状态。（`Translator` 冻结 committed snapshot 已实现并在 `54f0e9a` 实证 GREEN。）
- [ ] `.tmp` 的 `CREATE_NEW` 冲突会保护未知文件，但需要明确的恢复 UX，而不是静默删除非自有临时文件。
- [ ] `TYPETIDE_DATA_DIR` 下的进程内存凭据仅用于测试，不是持久化的生产路径。
