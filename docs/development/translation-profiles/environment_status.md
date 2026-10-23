# 翻译配置：环境与进度状态

状态日期：2026-10-21。**原生事务行为 RED 已在 commit `9c7b0e306ea87252eb7b4ed49a0f760d5f579369` 实证（run `39406689216` / job `118574219317`，21 failure(s)，job exit 1，无 infra exit 2），其实现已在 commit `60f0e4e` 实证 GREEN：run `39576810968` 的 [Native job `119144243876`](https://github.com/kanoha222/typetide/actions/runs/39576810968/job/119144243876) success，且同一 commit 全部 6 个 check-run success（Core `119144243672`、Guard `119144243628`、Windows `119144243368`、Mac `119144243054`、Brand `119144243309`）。下一阶段 `translation-execution` 已在 commit `a117a72` 实证行为 RED（run `39910106404` / [job `120270873137`](https://github.com/kanoha222/typetide/actions/runs/39910106404/job/120270873137)，configure/build 成功，314 failure(s)，exit 1）；纯函数实现已提交，云端 GREEN 尚未被观察到。** [文档索引](README.md) · [待办清单](task_list.md)

## 当前证据

- 基础设施 [PR #1](https://github.com/kanoha222/typetide/pull/1) 已合并。当前为 [draft PR #2](https://github.com/kanoha222/typetide/pull/2)，分支 `feat/translation-profiles-implementation`，auto-fix 已开启；不合并、不发版。
- 历史实证：
  - JSON 阶段 RED（commit `ea90e73`，30 failures）已于 commit `439b9ff` 实证 GREEN；
  - 完整 Profile 脚手架 RED（commit `708a593`，job `117972461843`，112 failures）；
  - 纯核心编解码与领域校验完整 GREEN（commit `df2fc7c`，run `39356274299`，job `118411675231`，0 failures）；
  - 原生存储真实 RED（commit `231883a`，run `39361446003`，job `118428324821`，10 failures）；
  - 原生存储写保护完整 GREEN（commit `f724b4c`，run `39364511799`，5/5 检查全部成功，job `118438131020` 0 failures）；
  - [原生事务真实行为 RED](https://github.com/kanoha222/typetide/actions/runs/39406689216/job/118574219317)（commit `9c7b0e3`，21 failures）：构建成功、无 fixture/infra 失败。主会话同时确认 Core、NativeGuard 与 Windows 基线仍 PASS。

## 原生事务实现状态（commit `60f0e4e` 已实证 GREEN）

以下描述的是原生事务批次；其云端执行证据见上文 Native job。Settings/UI/provider 的完整运行时集成、完整旧/新基线、mock/UIA 与实测覆盖率仍未验证。

- `SettingsData` 保存完整非递归值 ledger，保留既有公开字段为可编辑 draft。构造、成功 load、成功 commit 初始化已提交值；`snapshot()` 仅复制 ledger 的 profiles/connection/ref/epoch 和已解析 key，不读 draft，不做凭据 I/O。key 仅驻内存，不记录日志、不序列化；epoch 只驻运行时。
- schema 2 通过既有 Core `Decode`/`Encode`，完整保留 profiles、三个绑定与全局元数据。结构校验不调用 capability gate；不支持但结构有效的 BuiltIn Formal/Custom 仍可保存。损坏、空文件、超 1 MiB、未知 schema、I/O 失败保持全路径写锁，先于任何凭据访问。
- 缺失文件 Ready+Defaults+NoKey；v2 缺失/空 ref 不访问任何凭据。legacy 只 materialize，不自动写文件；非空 inline key 优先且不访问无关 fixed 记录，无 inline 时才读 fixed 并记录实际来源。只有成功显式提交后才清理已知旧 ref/实际读过的 fixed 来源。
- mutable `save()` 提交实际 receiver；const `save()` 仅允许完整值与已提交 v2 一致、无需迁移/ref/epoch 变更的幂等写。const 脏候选或非版本化 legacy 状态明确拒绝，不 cast 修改 const 对象、不发布 singleton。
- commit 先确定 CoCreateGuid 不透明 ref、序列化最终完整 JSON 并检查 UTF-8 实际字节数，再 stage 新凭据；沿用有效已知 ref 且 key 不变时不重复写凭据。legacy key 无 ref 时即便 key 不变也必须 stage。临时文件使用独占新建、完整 write/flush/close 检查和一次原子替换，无 Sleep；失败仅清理自己的 staged ref/临时文件并保留前驱。成功后一次发布完整值，再清理已知旧记录；清理失败保留成功提交并给出不含秘密的 operational warning。
- 正式 versioned facade 校验 1..64 ASCII alnum/`_`/`-` ref，规范化 ASCII 大小写，精确使用 `TypeTide/OpenAICompatibleApiKey/<opaqueRef>`。typed read 明确区分 Found、NotFound、Error，仅 WinCred `ERROR_NOT_FOUND` 属于不存在；v2 引用缺失记录或读错误均锁写，不静默改成 NoKey。blob 使用 SDK `CRED_MAX_CREDENTIAL_BLOB_SIZE` 字节上限。
- `TYPETIDE_DATA_DIR` 无注入 backend 时，生产 facade 使用按隔离目录分组的进程内存 store，支持旧 app self-test 跨 Settings 实例 roundtrip；不会写真实 CM，也不为测试把 key 写回 v2 JSON。有注入时使用 fake；fresh/invalid/v2-NoKey 不 probe backend，保留 strict NativeGuard 零调用契约。
- 原全部事务断言保留。新增 14 个针对 typed read 错误/NotFound、坏 ref/metadata、全局 roundtrip、const 边界、cleanup warning、blob/最终文件边界、外来候选私有 ref、已有临时文件所有权及大小写规范化的回归用例。**commit `60f0e4e` 的已通过 Native suite 经 `RunAdditionalCases` 包含上述 14 个新增用例；它们先于配套实现书写，但无独立观察到的 RED，亦无实测覆盖率，不能据此宣称完整功能验收。**
- Core/Json/App/UI/Translator/provider、已审 fixture 与原测试预期未改。root 和 standalone CMake 仅登记所需 Settings 模块及追加测试；事务目标仍仅链接 test facade/fake，绝不编译生产 `Credentials.cpp`，存储保护目标仍使用 strict double。

## 下一阶段：translation-execution（行为 RED 已实证，GREEN 待验证）

- 新增的纯模块：`TranslationExecution.*`（冻结的执行上下文、方向解析、四种 preset 提示词的字节金样、自定义模板渲染、Preview/Prepare、请求体与缓存身份）、`EndpointPolicy.*`（离线 authority 分类器）、`HttpUrl.*`（仅封装 `WinHttpCrackUrl` 的离线解析）。RED 阶段为返回 not-ready 值的编译壳；实现提交后：preset 提示词与旧 builder 逐字节一致，自定义模板单遍替换两个精确 token、未知 token 保持字面并只给固定 warning 码，分类器先做严格 ASCII authority 校验再与 WinHTTP 解析结果交叉核对 host/port/scheme，缓存身份为长度前缀字段且不含密钥/ref/profile ID/名称。`profiles::IsBlankText` 复用 `Validate` 的空白定义。全程不发送网络请求、不读取密钥、不调用服务。
- 测试目标 `TranslationExecutionSelfTest` 位于 `windows/tests/CMakeLists.txt`，只链接 Json/Language/Types/Profile/Store 与上述新模块（`winhttp` 仅用于离线解析）；不含 Settings/Credentials/Log/GUI/内置模型运行时。独立 job `translation-execution` 沿用已固定的 action SHA、`GITHUB_ENV` 步骤、`contents: read`、真实退出码传播、日志构件与 10 分钟超时。Root `CMakeLists.txt`、`Translator`、`Http.cpp`、`App`、`Settings` 与原有测试不变。
- 覆盖：preset OpenAI/Ollama 金样、自定义 OpenAI messages 与 Ollama `system`+`prompt` 分离、`think:false` 与 400 重试体、语言/动作方向与 retarget、模板 token（区分大小写、单遍、未知保持字面）、BuiltIn 能力拒绝、authority 矩阵、OpenAI 仅三种 loopback 主机允许 HTTP、Ollama 显式端口冲突拒绝、缓存身份隔离（含 epoch、改名不失效、密钥不入身份）。全部使用合成密钥与合成选中文字，无真实 API 调用。
- 状态：首次推送 `b2327d1` 因一处测试循环的 MSVC C3535 编译失败；仅修该测试后，commit `a117a72` 云端实证行为 RED（configure/build 成功、314 个断言失败、exit 1、无崩溃或 infra 失败）。随后的提交实现上述 helper，其云端 GREEN **尚未被观察到**；未本地构建/运行，未测覆盖率。

## 验证入口与剩余门槛

- [工作流](../../../.github/workflows/translation-profiles.yml) 分别运行 `ProfileCoreSelfTest`、`NativeStorageSelfTest`、`NativeTransactionSelfTest` 与新增的 `TranslationExecutionSelfTest`，保留真实失败退出码与日志构件。
- 本批没有本地 configure/build/run、工具安装/下载、模型/provider 调用、真实 CM 操作或 CI 轮询。execution 目标须在主会话独立审查并发布后，由云端给出实际 RED/GREEN；不得以源码审阅代替执行证据。
- 文件替换与凭据存储跨子系统，不声称断电时具备绝对原子性；崩溃可能留下无引用记录。本实现不枚举、不做启动扫删；只能清理有实际来源证据的记录。
- 后续仍需 Settings/UI/provider 完整集成、完整旧/新基线、mock/UIA、macOS 功能适配与完整验收及实测覆盖率 ≥80%（未测不得勾选通过）。公开日志仅含合成数据，不包含私人机器诊断、密钥、ref/hash 或选中文字。

## 集成门槛（待办，非已完成功能）

- 公开 `Settings` 字段仍是旧版可编辑 draft，生产 `Translator` 在下一次集成前仍读取它们；本批不宣称完整运行时激活。保存失败时可能保留未提交 draft，与旧行为一致。
- 功能验收前，UI 必须使用 candidate commit 并在提交后重新同步控件、处理 error + warning / 只读状态；`Translator` 必须冻结 committed snapshot。
- `.tmp` 的 `CREATE_NEW` 冲突保护未知文件，但需要明确的恢复 UX，不能静默删除非自有临时文件。
- `TYPETIDE_DATA_DIR` 下的进程内存凭据仅用于测试，不是持久化的生产路径。
- 以上均为后续任务；历史 RED 21 failure(s) 与运行时 GREEN 待验证状态不变。
