# 翻译配置：环境与进度状态

状态日期：2026-10-19。**完整 Profile 测试集 112 failures 行为 RED 已实证（commit 708a593）；领域模型与存储编解码已实施，完整 GREEN 待云端复跑验证。** [文档索引](README.md) · [待办清单](task_list.md)

## 当前证据

- 基础设施 [PR #1](https://github.com/kanoha222/typetide/pull/1) 已合并。当前为 [draft PR #2](https://github.com/kanoha222/typetide/pull/2)，分支 `feat/translation-profiles-implementation`。
- **JSON 阶段 GREEN 已实证**：提交 `439b9ff`，独立测试 job `Profile core self-test` 成功；既有 Windows build + self-test 亦成功。
- **完整行为 RED 已实证**：提交 `708a593` 在云端 Windows 独立测试 job [Profile core self-test](https://github.com/kanoha222/typetide/actions/runs/39226040932/job/117972461843) 中成功完成 configure/build，随后运行 `ProfileCoreSelfTest.exe` 报告 **112 failure(s)、退出码 1**，无崩溃，严格 JSON 边界测试仍全部通过。这是完整的实际领域行为 RED。
- **领域逻辑已实施（Phase 1c）**：
  - `TranslationProfile.cpp`：实施严格 UTF-8 解析与 Unicode 空白/控制字符分析，name/prompt/id/model 规范与边界校验、完整枚举合法性检查、64 配置上限与唯一性、绑定引用完整性、BuiltIn 仅 faithful preset 可执行能力校验、`Defaults`、`Find`、`BoundProfile`。
  - `TranslationProfileStore.cpp`：实施严格 `schemaVersion` 类型与整数范围校验、v2 schema 完整字段与枚举编解码、未版本化 legacy 配置（read/rewrite 独立语言、方向、风格、模型及 popup/replace 物理槽绑定）迁移逻辑。
  - 生产边界隔离：ROOT 生产 `windows/CMakeLists.txt`、`Settings`、`App`、`UI`、`Translator`、`Json.cpp` 保持不变，未接入生产应用；无 `NewId` 桩定义。
- 完整 GREEN 结果与实测覆盖率仍在待验证状态，不提前宣称通过。
- 本地 SDK 路线因完整性不匹配已放弃；使用 GitHub Windows runner 预装 MSVC/SDK/CMake。

## 当前 CI 入口与边界

- [工作流](../../../.github/workflows/translation-profiles.yml) 与[独立 CMake 项目](../../../windows/tests/CMakeLists.txt) 维持 `PROFILE_CORE_JSON_ONLY=OFF`，用于运行完整 `ProfileSelfTest.cpp` 契约测试套件。
- configure/build 预期通过；运行阶段验证领域实现与完整契约测试套件。
- draft PR #2 已启用 auto-fix；不合并、不发版。不连接真实网络 provider、不执行模型、不注入密钥、不采集选中文字；公开日志仅使用合成数据，不包含私人机器诊断。

## 后续门槛（按顺序）

1. 云端 Windows 独立测试复跑当前领域逻辑实现，验证完整契约测试套件（实证完整 GREEN）。
2. 在验证完整 GREEN 后，保持生产与设置代码隔离，完成 Phase 1 代码与安全审查后，再规划后续接入。
3. 补齐 macOS 最终状态、完整旧/新基线、mock/UIA、实测覆盖率和代码/安全审查；未执行项或不足如实保留，达到各项验收门槛后才更新任务状态。
