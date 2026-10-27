# 翻译配置：环境与进度状态

状态日期：2026-10-27。**原生事务行为 RED 已在 commit `9c7b0e306ea87252eb7b4ed49a0f760d5f579369` 实证（run `39406689216` / job `118574219317`，21 failure(s)，job exit 1，无 infra exit 2），其实现已在 commit `60f0e4e` 实证 GREEN：run `39576810968` 的 [Native job `119144243876`](https://github.com/kanoha222/typetide/actions/runs/39576810968/job/119144243876) success，且同一 commit 全部 6 个 check-run success（Core `119144243672`、Guard `119144243628`、Windows `119144243368`、Mac `119144243054`、Brand `119144243309`）。下一阶段 `translation-execution` 已在 commit `a117a72` 实证行为 RED（run `39910106404` / [job `120270873137`](https://github.com/kanoha222/typetide/actions/runs/39910106404/job/120270873137)，configure/build 成功，314 failure(s)，exit 1），其纯函数实现已在 commit `f819c2b` 实证 GREEN（run `39911587455` / [job `120275597655`](https://github.com/kanoha222/typetide/actions/runs/39911587455/job/120275597655)：0 failure(s)；同一 commit 全部 7 个 check-run success）。运行时集成已在 commit `43093f7` 实证行为 RED（run `39931004232`：纯函数 job `120338977896` 38 failure(s)，运行时 mock job `120338978040` 17 failure(s)，均 build 成功、exit 1），其实现已在 commit `54f0e9a` 实证 GREEN（run `40273382829`：纯函数 job `121410872214` 0 failure(s)，运行时 mock job `121410872452` 全部通过，8 个 check-run 全部 success）。** [文档索引](README.md) · [待办清单](task_list.md)

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

## translation-execution 纯函数（commit `f819c2b` 已实证 GREEN）

- 新增的纯模块：`TranslationExecution.*`（冻结的执行上下文、方向解析、四种 preset 提示词的字节金样、自定义模板渲染、Preview/Prepare、请求体与缓存身份）、`EndpointPolicy.*`（离线 authority 分类器）、`HttpUrl.*`（仅封装 `WinHttpCrackUrl` 的离线解析）。RED 阶段为返回 not-ready 值的编译壳；实现提交后：preset 提示词与旧 builder 逐字节一致，自定义模板单遍替换两个精确 token、未知 token 保持字面并只给固定 warning 码，分类器先做严格 ASCII authority 校验再与 WinHTTP 解析结果交叉核对 host/port/scheme，缓存身份为长度前缀字段且不含密钥/ref/profile ID/名称。`profiles::IsBlankText` 复用 `Validate` 的空白定义。全程不发送网络请求、不读取密钥、不调用服务。
- 测试目标 `TranslationExecutionSelfTest` 位于 `windows/tests/CMakeLists.txt`，只链接 Json/Language/Types/Profile/Store 与上述新模块（`winhttp` 仅用于离线解析）；不含 Settings/Credentials/Log/GUI/内置模型运行时。独立 job `translation-execution` 沿用已固定的 action SHA、`GITHUB_ENV` 步骤、`contents: read`、真实退出码传播、日志构件与 10 分钟超时。Root `CMakeLists.txt`、`Translator`、`Http.cpp`、`App`、`Settings` 与原有测试不变。
- 覆盖：preset OpenAI/Ollama 金样、自定义 OpenAI messages 与 Ollama `system`+`prompt` 分离、`think:false` 与 400 重试体、语言/动作方向与 retarget、模板 token（区分大小写、单遍、未知保持字面）、BuiltIn 能力拒绝、authority 矩阵、OpenAI 仅三种 loopback 主机允许 HTTP、Ollama 显式端口冲突拒绝、缓存身份隔离（含 epoch、改名不失效、密钥不入身份）。全部使用合成密钥与合成选中文字，无真实 API 调用。
- 状态：首次推送 `b2327d1` 因一处测试循环的 MSVC C3535 编译失败；仅修该测试后，commit `a117a72` 云端实证行为 RED（configure/build 成功、314 个断言失败、exit 1、无崩溃或 infra 失败）。commit `f819c2b` 实现上述 helper 并实证 GREEN（[job `120275597655`](https://github.com/kanoha222/typetide/actions/runs/39911587455/job/120275597655)，0 failure(s)，全部 7 个 check-run success）。这只证明纯函数契约；Translator/Http.cpp/UI 尚未接入，未本地构建/运行，未测覆盖率。已知差异（待后续统一）：保存时 `profiles::Validate` 的未知变量警告对 `{{}}` 等形状比渲染器更宽。

## 运行时集成（RED `43093f7`，GREEN `54f0e9a`）

- 触发：快捷键物理槽位 1/2 与划词入口在主线程用 `translator::FreezeEntry` 冻结**已提交**快照中绑定的 profile 与连接（不读 draft）；槽位配置的动作决定方向与弹窗/替换。取词（含剪贴板兜底）、重试与改目标语言都沿用同一冻结 context；之后保存的 profile/连接只影响下一次触发。槽位动作与改写预览开关和快捷键注册一样读当前设置，但在触发时就冻结，异步取词期间的修改不影响这次触发；每次触发有序号，晚到的取词若期间已有更新的触发则丢弃。绑定无法解析时显示固定错误，不回退到其他 profile。
- `Translator`：主线程 `execution::Prepare` 决定 URL/请求体/缓存身份，失败给出固定文案（不含 URL/密钥/提示词）；工作线程只发送冻结的请求，Ollama 400 时发送预先构造的 retry body。缓存键为 `cacheIdentity`，缓存与交付使用 `execution::DeliveredOutput`：custom 原样（缩进/制表符/结尾换行），preset 保留旧的去首尾空白。旧的按全局字段拼请求的 `Stream/TranslateFully(TranslationRequest)` 已删除；`SystemPrompt/PlainPrompt` 转发到唯一的 preset 实现。
- `Http.cpp`：改用共享 `CrackUrl`（去掉 256/2048 固定缓冲，保留 query），provider POST 设置 `WINHTTP_OPTION_REDIRECT_POLICY_NEVER`，3xx 作为错误报告而不跟随。`Get`（更新检查、Ollama 模型列表）保持跟随重定向。
- App：新的用户翻译使上一次失效（取消其流，丢弃尚未粘贴的直接替换结果）；直接替换会关闭被取代的弹窗，避免停在半截译文上还能点 Replace；直接替换与弹窗会话分开跟踪，关闭旧弹窗不会取消新的直接替换。Ollama 预加载只预热已提交 profile 实际绑定的模型（去重）。完整的弹窗关闭原因与 TextReplacer 最终边界守卫属于 Phase 3，尚未实现。
- 连接测试（设置窗口 Backend 页）仍按该页正在编辑的 backend/model/连接用固定合成句测试，不使用 profile 或选中文字。
- **过渡期（用户选择）**：旧设置窗口的 backend/model/语言/方向/风格控件已不影响翻译，连接字段（Ollama host/port、API base URL/key）仍有效；profile 编辑器（Phase 4）完成前需编辑 settings.json 修改 profile 与绑定。
- 验证：`TypeTide.exe --selftest-runtime`（job `translation-runtime`）用真实 Translator + WinHTTP 对本机 mock（`windows/scripts/mock-openai.ps1`，只记录请求路径/请求体/是否带 Authorization，不记录其值）检查线上请求体与 Prepare 一致、custom 原样输出、think 重试、缓存与 epoch 隔离、拒绝重定向、拒绝远程明文 HTTP、超过 2048 字符的 URL 及已提交设置到快捷键的路径。

## Phase 3：会话生命周期与托盘绑定（RED `6e7c858`，GREEN `3de750f`）

- 行为 RED：run `40277449813`，纯函数 [job `121423092965`](https://github.com/kanoha222/typetide/actions/runs/40277449813/job/121423092965) 19 failure(s)，运行时 mock [job `121423093049`](https://github.com/kanoha222/typetide/actions/runs/40277449813/job/121423093049) 5 failure(s)，均 build 成功、exit 1。实现于 commit `3de750f` 实证 GREEN：run `40444337477`，纯函数 [job `121991887469`](https://github.com/kanoha222/typetide/actions/runs/40444337477/job/121991887469) 0 failure(s)，运行时 mock [job `121991887732`](https://github.com/kanoha222/typetide/actions/runs/40444337477/job/121991887732) 全部通过，8 个 check-run 全部 success。
- 独立审查后的修正：托盘绑定的 candidate 以已提交值为基础，只改该绑定，不提交也不丢弃其他未保存的 draft；Ctrl+C 兜底在触发时记录前台窗口，等剪贴板事务锁期间前台若已切换就不复制；退出时 AppExit 也取消没有弹窗的当前会话、注销快捷键，并在剪贴板事务仍在还原时短暂轮询（UI 线程只 try_lock，最长约 3 秒）；被拒绝的触发（超长、无绑定）同样使之前未落下的粘贴失效。
- `session::Ledger`（纯函数，主线程）：每次用户翻译开新会话，旧会话的请求被取消、未提交的粘贴失效；流回调只带创建时的 `Ticket`（会话 + 尝试序号），因此同步缓存命中、重试与改目标语言的旧回调都能被丢弃。弹窗关闭带原因：Dismiss 取消本会话，ReplaceCommit 关闭弹窗但保留粘贴，Superseded 只作废被取代的会话，AppExit 取消当前会话；过期弹窗的关闭不会影响新会话。
- 替换的最后边界：`replacer` 在动剪贴板前、Ctrl+A 前、Ctrl+V 前各核对一次 `PasteGuard`（会话仍是最新且取词时的前台窗口仍在前台），否则放弃并还原剪贴板；Ctrl+V 发出后无法撤回。取词兜底（Ctrl+C）与替换用同一把剪贴板事务锁串行，只在工作线程上等待。取词时记录前台窗口（`Capture::window`）。
- 托盘新增“Shortcut 1 / Shortcut 2 / Selection profile”三个子菜单，列出已提交的 profile 并勾选当前绑定；选择后经 `profilebinding::Commit`（candidate commit，失败弹出原因）保存，只影响之后的触发。
- 验证：纯函数测试覆盖上述会话序列与 `profiles::WithBinding`；`--selftest-runtime` 覆盖托盘绑定提交、替换中止后剪贴板不变（过期会话 / 目标窗口不在前台）、请求进行中改连接时缓存按 epoch 分开。真实按键粘贴路径与弹窗交互的 UIA 端到端测试仍未运行。

## 验证入口与剩余门槛

- [工作流](../../../.github/workflows/translation-profiles.yml) 分别运行 `ProfileCoreSelfTest`、`NativeStorageSelfTest`、`NativeTransactionSelfTest` 与新增的 `TranslationExecutionSelfTest`，保留真实失败退出码与日志构件。
- 本批没有本地 configure/build/run、工具安装/下载、模型/provider 调用、真实 CM 操作或 CI 轮询。execution 目标须在主会话独立审查并发布后，由云端给出实际 RED/GREEN；不得以源码审阅代替执行证据。
- 文件替换与凭据存储跨子系统，不声称断电时具备绝对原子性；崩溃可能留下无引用记录。本实现不枚举、不做启动扫删；只能清理有实际来源证据的记录。
- 后续仍需 Settings/UI/provider 完整集成、完整旧/新基线、mock/UIA、macOS 功能适配与完整验收及实测覆盖率 ≥80%（未测不得勾选通过）。公开日志仅含合成数据，不包含私人机器诊断、密钥、ref/hash 或选中文字。

## 集成门槛（待办，非已完成功能）

- 公开 `Settings` 字段仍是旧版可编辑 draft。翻译执行已改为冻结 committed snapshot（不再读 draft，GREEN `54f0e9a`）；仅设置窗口的连接测试仍有意读取正在编辑的值。保存失败时可能保留未提交 draft，与旧行为一致。
- 功能验收前，UI 必须使用 candidate commit 并在提交后重新同步控件、处理 error + warning / 只读状态。
- `.tmp` 的 `CREATE_NEW` 冲突保护未知文件，但需要明确的恢复 UX，不能静默删除非自有临时文件。
- `TYPETIDE_DATA_DIR` 下的进程内存凭据仅用于测试，不是持久化的生产路径。
- 以上均为后续任务；历史 RED 21 failure(s) 与运行时 GREEN 待验证状态不变。
