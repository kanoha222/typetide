# 翻译配置：环境与进度状态

状态日期：2026-10-20。**已验证基线仍为 commit `f724b4c`：原生存储写保护 5/5 check-runs GREEN（run 39364511799，job 118438131020，0 failures）。当前增量仅为原生事务 RED 脚手架修订，尚未 configure/build/run，也尚无事务行为 RED 实证；`commit()` 明确返回 `false` / `NotReady`，完整事务实现必须等待真实云端 RED。** [文档索引](README.md) · [待办清单](task_list.md)

## 当前证据

- 基础设施 [PR #1](https://github.com/kanoha222/typetide/pull/1) 已合并。当前为 [draft PR #2](https://github.com/kanoha222/typetide/pull/2)，分支 `feat/translation-profiles-implementation`。
- **历史演进记录**：
  - JSON 阶段 RED（commit `ea90e73`，30 failures）已于 commit `439b9ff` 实证 GREEN；
  - 完整 Profile 脚手架 RED（commit `708a593`，job `117972461843`，112 failures）实证了桩阶段的完整领域契约 RED；
  - 纯核心编解码与领域校验完整 GREEN（commit `df2fc7c`，run `39356274299`，job `118411675231`，0 failures）；
  - 原生存储真实 RED（commit `231883a`，run `39361446003`，job `118428324821`，10 failures）；
  - 原生存储写保护完整 GREEN（commit `f724b4c`，run `39364511799`，5/5 检查全部成功，job `118438131020` 0 failures）。
- **原生事务 RED 脚手架（仅源码检查，当前未执行）**：
  - 测试目标：`NativeTransactionSelfTest.exe`；主用例、迁移用例及公共断言分别位于 `NativeTransactionSelfTest.cpp`、`NativeTransactionMigrationTests.cpp`、`NativeTransactionSupport.h/.cpp`。
  - 链接隔离：事务目标仅链接 `TestCredentialFacade.cpp` 与 `TestCredentialStoreSeam.h/.cpp`，不编译生产 `Credentials.cpp`，也不链接 WinCred 导入库。`ScopedCredentialStore` 在正常/异常退出时恢复先前 backend；每用例独立 fake，事件记录包含操作顺序、ref 和结果，不包含密钥，也不在事务中途重置计数。原生存储保护目标继续使用记录所有非预期新旧调用的 strict double；两个测试目标均不链接生产凭据实现。
  - 回滚契约：stage-failure 必须观察到真实 stage 尝试；rename-failure 必须先成功提交有效前驱文件/ref/key/epoch，再用 `OPEN_EXISTING` 独占锁阻止真实替换，检查锁关闭、成功 stage 的新 ref 随后删除及旧 ref/key/文件/runtime 保留。NotReady 导致的 seed 失败是断言失败并安全返回该依赖用例，不误报 infra；其余独立用例继续，fixture 仍检查清理。
  - 边界契约：33 个唯一 ID、每个 prompt 不超过 32768 UTF-16 单元的有效 profile，先确认校验通过且实际 JSON >1 MiB，再要求提交在 stage/tmp 写入前拒绝。有效 v2 编码覆盖缺失和空 ref 两种 Ready+NoKey、零 legacy/versioned 读取；future schema 和缺失文件保持无凭据访问。
  - 密钥/迁移契约：两次成功提交才能验证仅改名称时 JSON 确实更新、非空 ref/stage 计数/epoch 稳定；清空必须从非空已提交密钥开始，只删已知旧 ref、epoch 增加、JSON 为 NoKey 且重载不回退 fixed；fresh NoKey→empty 不增加 epoch。inline legacy 优先且不读删无关 fixed；只有已知 fixed 来源在成功持久化后才可删除；stage 失败和成功 stage 后的真实 rename 失败均要求保留来源。迁移 ref 必须为 1..64 ASCII 字母数字/`_`/`-` 的不透明后缀，map 值、v2 profiles、无明文及重载一致性均有断言。
  - 发布/快照契约：隐私检查以前置成功提交、可读 JSON 和非空 ref 为条件；非法候选比较完整 runtime/profile/connection/key/file/epoch；快照用例先提交，再不保存地修改 live endpoint/key/profile，要求后取快照仍是最后提交值。DTO 的 resolved `openAIKey` **仅驻内存，不新增序列化或日志字段**。
  - 生产边界：`profileConfig` 使用真实 Defaults，共用枚举位值不变，epoch/ref 私有。`commit()` 仅返回 `false` / `NotReady`，`snapshot()` 仍为待实现的 value shell，无生产调用。旧 `save()`/`load()` 与无注入 backend 时的旧 legacy WinCred facade 保持原行为。新增 version-ref/migration helpers 先做纯 ASCII ref 校验，仅调用注入 fake；无 backend 时 fail closed，不开启真实 WinCred 新 API，不实现 UTF-8 target 转换。`LoadKey` 的空字符串尚不能区分 NotFound/Failure，明确留待真实 RED 后修正，未宣称解决。
  - 构建与 CI：root app 与两个原生测试目标登记 Core 三个 cpp 及既有依赖；事务目标登记独立 facade、fake 和测试 helper；独立 `native-transaction` job 保留真实退出码和日志。没有本地 configure/build/run、工具安装/下载、真实凭据/网络调用或 CI 轮询；事务 RED 与任何新 GREEN/覆盖率证据均待主会话发布后在云端取得。

## 当前 CI 入口与边界

- [工作流](../../../.github/workflows/translation-profiles.yml) 包含三个独立 Windows 任务：
  - `profile-core`：运行完整纯核心契约测试套件（`ProfileCoreSelfTest.exe`，已实证 GREEN）；
  - `native-storage`：运行原生设置存储黑盒保护测试套件（`NativeStorageSelfTest.exe`，已实证 GREEN）；
  - `native-transaction`：运行原生事务提交与隔离测试套件（`NativeTransactionSelfTest.exe`，待云端复跑实证 RED）。
- 失败不掩盖（`exit $LASTEXITCODE`），日志均作为构件留存。
- draft PR #2 已启用 auto-fix；不合并、不发版。不连接真实网络 provider、不执行模型、不注入密钥、不采集选中文字；公开日志仅使用合成数据，不包含私人机器诊断。

## 后续门槛（按顺序）

1. 云端 Windows 独立测试复跑 `native-transaction`，实证原生事务行为 RED。
2. 在验证原生事务 RED 后，实现完整 profile-aware 提交、版本化凭证暂存与旧目标安全迁移。
3. 补齐 macOS 最终状态、完整旧/新基线、mock/UIA、实测覆盖率和代码/安全审查；未执行项或不足如实保留，达到各项验收门槛后才更新任务状态。
