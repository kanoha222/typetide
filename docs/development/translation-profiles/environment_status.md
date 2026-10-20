# 翻译配置：环境与进度状态

状态日期：2026-10-20。**云端 Windows 独立测试在 commit d469920 观测到 1 项真实失败（唯一失败：`duplicate IDs rejected while decoding`）；现已完成 Encode fail-open 根因针对性修复与回归追加，完整 GREEN 待云端复跑验证。** [文档索引](README.md) · [待办清单](task_list.md)

## 当前证据

- 基础设施 [PR #1](https://github.com/kanoha222/typetide/pull/1) 已合并。当前为 [draft PR #2](https://github.com/kanoha222/typetide/pull/2)，分支 `feat/translation-profiles-implementation`。
- **历史 RED 演进记录**：
  - JSON 阶段 RED（commit `ea90e73`，30 failures）已于 commit `439b9ff` 实证 GREEN；
  - 完整 Profile 脚手架 RED（commit `708a593`，job `117972461843`，112 failures）实证了桩阶段的完整领域契约 RED。
- **最新实际测试证据（commit `d469920`）**：
  - 云端 Windows 独立测试 job [Profile core self-test](https://github.com/kanoha222/typetide/actions/runs/39233983588/job/117998489088) 成功完成 configure 与 build，运行 `ProfileCoreSelfTest.exe` 报告 **1 failure(s)、退出码 1**。
  - Windows 原生构建自测、Mac 测试与品牌检查 baseline 均 pass。
  - **唯一失败用例**：`duplicate IDs rejected while decoding`，其余全部契约断言均已通过。
- **根因分析与最小修复（Phase 1c Bugfix）**：
  - 根因：`Encode` 在输入配置未通过校验时原先返回 `{}`，而 `Decode({})` 按合同属于合法未版本化旧配置（回退为 `MigratedLegacy` 默认值），形成 fail-open 组合，导致解码状态为 `MigratedLegacy` 而非 `Invalid`。
  - 最小修复：`Encode` 始终先构造 `root["schemaVersion"] = 2;`，结构无效时返回仅含 v2 标记但不含 profile 数据的 invalid sentinel（`{ "schemaVersion": 2 }`），使 `Decode` 严格判定为 `Invalid` 而绝不回退至 legacy。
  - 追加回归用例：1) 覆盖 `emptyConfig` 与 `badBinding` 编码后解码必为 `Invalid`；2) 直接反序列化包含重复 ID 的合法结构 v2 JSON（绑定显式解析有效），验证 `Decode` 原生拒绝重复 ID。
  - 生产边界隔离：ROOT 生产 `windows/CMakeLists.txt`、`Settings`、`App`、`UI`、`Translator`、`Json.cpp` 保持不变，未接入生产应用；无 `NewId` 桩定义。
- 完整 GREEN 结果仍在待验证状态，不提前宣称通过。
- 本地 SDK 路线因完整性不匹配已放弃；使用 GitHub Windows runner 预装 MSVC/SDK/CMake。

## 当前 CI 入口与边界

- [工作流](../../../.github/workflows/translation-profiles.yml) 与[独立 CMake 项目](../../../windows/tests/CMakeLists.txt) 维持 `PROFILE_CORE_JSON_ONLY=OFF`，用于运行完整 `ProfileSelfTest.cpp` 契约测试套件。
- configure/build 预期通过；运行阶段验证针对性 bugfix 与完整契约测试套件。
- draft PR #2 已启用 auto-fix；不合并、不发版。不连接真实网络 provider、不执行模型、不注入密钥、不采集选中文字；公开日志仅使用合成数据，不包含私人机器诊断。

## 后续门槛（按顺序）

1. 云端 Windows 独立测试复跑针对性修复与回归用例，验证完整契约测试套件（实证完整 GREEN）。
2. 在验证完整 GREEN 后，保持生产与设置代码隔离，完成 Phase 1 代码与安全审查后，再规划后续接入。
3. 补齐 macOS 最终状态、完整旧/新基线、mock/UIA、实测覆盖率和代码/安全审查；未执行项或不足如实保留，达到各项验收门槛后才更新任务状态。
