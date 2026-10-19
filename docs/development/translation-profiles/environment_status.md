# 翻译配置：环境与进度状态

状态日期：2026-10-19。**云端 Windows 已观察到 JSON-only 行为 RED；仅 JSON 最小修复进行中，GREEN 与翻译配置功能仍未验证。** [文档索引](README.md) · [待办清单](task_list.md)

## 当前证据

- 基础设施 [PR #1](https://github.com/kanoha222/typetide/pull/1) 已合并。当前为 [draft PR #2](https://github.com/kanoha222/typetide/pull/2)，分支 `feat/translation-profiles-implementation`；提交 [ea90e73](https://github.com/kanoha222/typetide/commit/ea90e73) 修复了无效 CI context。
- [实际失败的 Windows job](https://github.com/kanoha222/typetide/actions/runs/39193075553/job/117869875089) 已完成 configure/build，随后运行 `ProfileCoreSelfTest.exe`，报告 **30 个断言失败、退出码 1**。这是实际行为 **RED**，不是环境、配置或编译失败。
- 失败类别：完整输入消费（full input consumption）、非法 JSON 数字/非有限值、无效 UTF-8/孤立代理项、未转义控制字符/值外 NUL、深度 >64。有效 Unicode、有效代理对、NUL roundtrip 与深度 64 用例仍通过；这些局部通过不代表整套 GREEN。
- 既有 Windows 应用 self-test 已通过；macOS 在上次读取时仍为 pending，尚未确认最终结果。完整旧/新基线、provider mock、UIA E2E 与实测覆盖率仍未验证。
- 已授权核心负责人仅修复 `windows/src/Json.cpp` / `windows/src/Json.h`，当前进行中。GREEN、完整 profile codec/store/migration、Settings、原生 UI 与 provider 功能实现均未验证，Phase 1–5 的整项任务仍待办。
- 本地 SDK 路线因完整性不匹配已放弃，不再列为活动阻塞；当前使用 GitHub Windows runner 预装的 MSVC、Windows SDK 与 CMake。

## 当前 CI 入口与边界

- [工作流](../../../.github/workflows/translation-profiles.yml) 与[独立 CMake 项目](../../../windows/tests/CMakeLists.txt) 提供隔离的控制台测试入口；本次证据来自 `PROFILE_CORE_JSON_ONLY=ON`，不是完整 profile/store/migration 测试的 RED 或 GREEN。
- configure/build/self-test 证据分开记录；不忽略非零退出码，不把预期失败改成成功。JSON-only RED 仅解除 JSON 最小修复的等待条件，不完成 Phase 1 的广泛 RED 清单。
- draft PR #2 已启用 auto-fix；不合并、不发版。不连接真实网络 provider、不执行模型、不注入密钥、不采集选中文字；公开日志仅使用合成数据，不包含私人机器诊断。

## 后续门槛（按顺序）

1. 核心负责人完成限定 JSON 修复后，以云端 Windows 复跑结果确认 JSON-only GREEN；当前不得预先宣称通过。
2. 完整 profile codec/store/migration 仍须先取得各自行为 RED，再进行对应实现并验证 GREEN；Settings/UI/provider 功能与集成同样保留待验证状态。
3. 补齐 macOS 最终状态、完整旧/新基线、mock/UIA、实测覆盖率和代码/安全审查；未执行项或不足如实保留，达到各项验收门槛后才更新任务状态。
