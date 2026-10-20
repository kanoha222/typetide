# 翻译配置：环境与进度状态

状态日期：2026-10-20。**云端 Windows 独立测试在 commit 231883a（run 39361446003, job 118428324821）实证 10 项真实 RED（5 项黑盒保护用例各 2 处断言失败，退出码 1；缺少文件初始安装对照与凭证隔离均通过）；现已完成原生持久化存储保护状态（`StorageState`）最小实现与 v2 暂只读过渡回归追加，完整 GREEN 待云端复跑验证。** [文档索引](README.md) · [待办清单](task_list.md)

## 当前证据

- 基础设施 [PR #1](https://github.com/kanoha222/typetide/pull/1) 已合并。当前为 [draft PR #2](https://github.com/kanoha222/typetide/pull/2)，分支 `feat/translation-profiles-implementation`。
- **历史演进记录**：
  - JSON 阶段 RED（commit `ea90e73`，30 failures）已于 commit `439b9ff` 实证 GREEN；
  - 完整 Profile 脚手架 RED（commit `708a593`，job `117972461843`，112 failures）实证了桩阶段的完整领域契约 RED；
  - 纯核心编解码与领域校验完整 GREEN（commit `df2fc7c`，run `39356274299`，job `118411675231`，0 failures）。
- **原生存储真实 RED 实际证据（commit `231883a`）**：
  - 云端 CI [run 39361446003](https://github.com/kanoha222/typetide/actions/runs/39361446003) job [Native settings storage self-test](https://github.com/kanoha222/typetide/actions/runs/39361446003/job/118428324821) 成功运行 `NativeStorageSelfTest.exe`，报告 **10 failure(s)、退出码 1**，无 infra error；
  - 5 项黑盒写保护用例（未来 schema 3、损坏 JSON、零字节文件、超大文件、读锁 IO 错误）各 2 项断言失败（旧 save() 返回 true 且覆盖文件），实证了旧 Settings 盲目写覆盖的行为缺陷；
  - 缺少文件初始安装对照 4 项断言通过，链接期凭证 double 意外调用计数 0 通过，纯核心测试、Windows 构建与品牌检查均 success。
- **原生存储保护实现证据（当前待实际跑）**：
  - 在 `Settings.h` 与 `Settings.cpp` 中引入私有持久状态 `StorageState`（`Ready`, `Invalid`, `UnsupportedSchema`, `IoError`）及只读属性；
  - `load()` 中对缺失文件（仅限 `ERROR_FILE_NOT_FOUND` / `ERROR_PATH_NOT_FOUND`）判定为 `Ready`，对打开失败、空文件、超大文件、非对象/损坏 JSON 严格记录 `Invalid` 或 `IoError`；
  - 数据安全保护：当前 native 尚未 profile-aware，对任何带有 `schemaVersion` 的文件（含 v2）严格 fail-closed 为 `UnsupportedSchema`，彻底防止扁平全局设置读入后保存覆盖销毁 profiles/bindings；明确 v2 暂设只读保护，后续阶段再接入完整 profile 事务；
  - `save()` 入口先行检查 `storageState_ == StorageState::Ready`，非 Ready 状态全路径拒绝写入，持久锁不被清除；
  - 追加 v2 过渡回归用例：验证 v2 文件在当前 guard 阶段拒 save 且保 bytes 不变，同时兼容未来 profile-aware 阶段 save 成功并保 profiles/bindings；
  - 保持未版本化合法旧配置的平稳读写，生产 `Credentials`、`Util`、`Log`、根 CMake 与核心类型保持完全未修改；
  - 状态：实现已就绪，待云端复跑验证实际 GREEN。

## 当前 CI 入口与边界

- [工作流](../../../.github/workflows/translation-profiles.yml) 包含两个独立 Windows 任务：
  - `profile-core`：运行完整纯核心契约测试套件（`ProfileCoreSelfTest.exe`，已实证 GREEN）；
  - `native-storage`：运行原生设置存储黑盒保护测试套件（`NativeStorageSelfTest.exe`，待云端复跑实证 GREEN）。
- 失败不掩盖（`exit $LASTEXITCODE`），日志均作为构件留存。
- draft PR #2 已启用 auto-fix；不合并、不发版。不连接真实网络 provider、不执行模型、不注入密钥、不采集选中文字；公开日志仅使用合成数据，不包含私人机器诊断。

## 后续门槛（按顺序）

1. 云端 Windows 独立测试复跑 `native-storage`，实证原生存储黑盒行为 GREEN。
2. 在验证原生存储安全落地后，开展 Phase 2/Profile 事务接入与 CM ref 方案。
3. 补齐 macOS 最终状态、完整旧/新基线、mock/UIA、实测覆盖率和代码/安全审查；未执行项或不足如实保留，达到各项验收门槛后才更新任务状态。
