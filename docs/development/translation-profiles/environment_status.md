# 翻译配置：环境与进度状态

状态日期：2026-10-19。**云端 Windows JSON 阶段已实证 GREEN（commit 439b9ff）；profile-store RED 脚手架已引入，完整 profile 行为 RED 待云端复跑验证。** [文档索引](README.md) · [待办清单](task_list.md)

## 当前证据

- 基础设施 [PR #1](https://github.com/kanoha222/typetide/pull/1) 已合并。当前为 [draft PR #2](https://github.com/kanoha222/typetide/pull/2)，分支 `feat/translation-profiles-implementation`。
- **JSON 阶段 GREEN 已实证**：提交 `439b9ff` 修复严格 JSON 边界后，云端 Windows 独立测试 job [Profile core self-test (JSON baseline)](https://github.com/kanoha222/typetide/actions/runs/39221251003/job/117956815530) status=completed, conclusion=success；既有 Windows build + self-test 亦成功 ([job 117956814963](https://github.com/kanoha222/typetide/actions/runs/39221251010/job/117956814963))。macOS CI 当时为 in_progress，不声明通过。
- **Profile-store RED 脚手架已就绪**：获得 coordinator 的 red-scaffold greenlight，引入最小可编译桩模块 `TranslationTypes.cpp`、`TranslationProfile.cpp`、`TranslationProfileStore.cpp`，在 `windows/tests/CMakeLists.txt` 与 `.github/workflows/translation-profiles.yml` 中登记源码并切换为 `PROFILE_CORE_JSON_ONLY=OFF`。
- **预期行为 RED（尚未运行）**：基础枚举转换、默认配置数据与查找辅助函数的正向用例可能成功；依赖未实现的领域校验器（`Validate`、`ValidateCapability`）、存储编解码（`Decode`、`Encode`）及迁移逻辑的断言预期自然失败。已知空结果索引访问的守卫已有静态证明；当前脚手架的 Windows 编译、链接、断言结果与崩溃情况均尚未实际观测，不作普遍无崩溃保证。
- 完整旧/新基线、provider mock、UIA E2E 与实测覆盖率仍未验证。不接入生产应用（ROOT `windows/CMakeLists.txt`、`Settings`、`App`、`UI`、`Translator`、`Json.cpp` 保持不变）。
- 本地 SDK 路线因完整性不匹配已放弃；使用 GitHub Windows runner 预装 MSVC/SDK/CMake。

## 当前 CI 入口与边界

- [工作流](../../../.github/workflows/translation-profiles.yml) 与[独立 CMake 项目](../../../windows/tests/CMakeLists.txt) 已切换至 `PROFILE_CORE_JSON_ONLY=OFF`，用于运行完整 `ProfileSelfTest.cpp` 测试套件。
- configure/build 预期通过；运行阶段因领域逻辑未实现预期产生实际断言失败与非零退出码（行为 RED）。
- draft PR #2 已启用 auto-fix；不合并、不发版。不连接真实网络 provider、不执行模型、不注入密钥、不采集选中文字；公开日志仅使用合成数据，不包含私人机器诊断。

## 后续门槛（按顺序）

1. 云端 Windows 跑出完整 profile 测试集的实际断言失败与非零退出码（实证完整行为 RED）。
2. 在观察到完整行为 RED 后，再开展 Phase 1c 领域逻辑、校验器与 v2/legacy 存储编解码实现，走向 GREEN。
3. 补齐 macOS 最终状态、完整旧/新基线、mock/UIA、实测覆盖率和代码/安全审查；未执行项或不足如实保留，达到各项验收门槛后才更新任务状态。
