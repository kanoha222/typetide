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
- 连接测试（设置窗口 Translation 页）按已绑定 profile 的 backend + 模型与已提交的连接用固定合成句测试，不使用 profile 提示词、剪贴板或选中文字（Phase 4 第 3 步）。
- **过渡期（用户选择，已结束）**：旧设置窗口的 backend/model/语言/方向/风格控件不再影响翻译；profile 编辑器（`20abd53` GREEN）已可编辑 profile 与绑定，第 3 步删除这些旧控件。
- 验证：`TypeTide.exe --selftest-runtime`（job `translation-runtime`）用真实 Translator + WinHTTP 对本机 mock（`windows/scripts/mock-openai.ps1`，只记录请求路径/请求体/是否带 Authorization，不记录其值）检查线上请求体与 Prepare 一致、custom 原样输出、think 重试、缓存与 epoch 隔离、拒绝重定向、拒绝远程明文 HTTP、超过 2048 字符的 URL 及已提交设置到快捷键的路径。

## Phase 3：会话生命周期与托盘绑定（RED `6e7c858`，GREEN `3de750f`）

- 行为 RED：run `40277449813`，纯函数 [job `121423092965`](https://github.com/kanoha222/typetide/actions/runs/40277449813/job/121423092965) 19 failure(s)，运行时 mock [job `121423093049`](https://github.com/kanoha222/typetide/actions/runs/40277449813/job/121423093049) 5 failure(s)，均 build 成功、exit 1。实现于 commit `3de750f` 实证 GREEN：run `40444337477`，纯函数 [job `121991887469`](https://github.com/kanoha222/typetide/actions/runs/40444337477/job/121991887469) 0 failure(s)，运行时 mock [job `121991887732`](https://github.com/kanoha222/typetide/actions/runs/40444337477/job/121991887732) 全部通过，8 个 check-run 全部 success。
- 独立审查后的修正：托盘绑定的 candidate 以已提交值为基础，只改该绑定，不提交也不丢弃其他未保存的 draft；Ctrl+C 兜底在触发时记录前台窗口，等剪贴板事务锁期间前台若已切换就不复制；退出时 AppExit 也取消没有弹窗的当前会话、注销快捷键，并在剪贴板事务仍在还原时短暂轮询（UI 线程只 try_lock，最长约 3 秒）；被拒绝的触发（超长、无绑定）同样使之前未落下的粘贴失效。
- `session::Ledger`（纯函数，主线程）：每次用户翻译开新会话，旧会话的请求被取消、未提交的粘贴失效；流回调只带创建时的 `Ticket`（会话 + 尝试序号），因此同步缓存命中、重试与改目标语言的旧回调都能被丢弃。弹窗关闭带原因：Dismiss 取消本会话，ReplaceCommit 关闭弹窗但保留粘贴，Superseded 只作废被取代的会话，AppExit 取消当前会话；过期弹窗的关闭不会影响新会话。
- 替换的最后边界：`replacer` 在动剪贴板前、Ctrl+A 前、Ctrl+V 前各核对一次 `PasteGuard`（会话仍是最新且取词时的前台窗口仍在前台），否则放弃并还原剪贴板；Ctrl+V 发出后无法撤回。取词兜底（Ctrl+C）与替换用同一把剪贴板事务锁串行，只在工作线程上等待。取词时记录前台窗口（`Capture::window`）。
- 托盘新增“Shortcut 1 / Shortcut 2 / Selection profile”三个子菜单，列出已提交的 profile 并勾选当前绑定；选择后经 `profilebinding::Commit`（candidate commit，失败弹出原因）保存，只影响之后的触发。
- 验证：纯函数测试覆盖上述会话序列与 `profiles::WithBinding`；`--selftest-runtime` 覆盖托盘绑定提交、替换中止后剪贴板不变（过期会话 / 目标窗口不在前台）、请求进行中改连接时缓存按 epoch 分开。真实按键粘贴路径与弹窗交互的 UIA 端到端测试仍未运行。

## Phase 4：原生 profile 编辑器（GREEN `20abd53`、`d8793f7`）

- 第 1 步纯函数编辑器模型：RED `d6e3f55`（29 failure(s)），实现 `df0b8b3`，在 `209a345` 的纯函数 job 实证 GREEN。
- 第 2 步原生窗口：编辑器端到端 job 在 `209a345` 实证行为 RED（[job `122003876635`](https://github.com/kanoha222/typetide/actions/runs/40448037403/job/122003876635)：build 成功，唯一失败 “the profile editor opens”）。实现：单实例 `TranslationProfilesWindow`（托盘“Manage profiles…”与 `--open-profiles` 打开），左侧列表 + 新增/复制/删除，右侧名称、语言对、方向、backend/模型、本地/网络/云端路由说明、preset/custom 切换、多行提示词与变量说明、有效请求预览，底部三个入口绑定、固定文本测试、保存/关闭。草稿与运行时隔离：保存经 `profilebinding::CommitConfiguration`（以已提交值为基础，不提交也不丢弃其他未保存 draft；编辑器传入基线，若 profile 在别处被改则拒绝保存并说明）。关闭/切换时有未保存修改会询问保存/放弃/取消；删除被绑定的 profile 必须选择替换，最后一个不可删；悬空绑定在打开时提示。超长粘贴导致提示词被截断后为粘滞拒绝（必须清空重填），无法无损编辑的提示词禁用编辑并原样保留；固定文本测试只用合成句，结果按窗口与测试代次丢弃过期结果。[实证 GREEN](https://github.com/kanoha222/typetide/actions/runs/40487268849/job/122132082264)（commit `20abd53`，22 项检查全部通过，9 个 check-run 全部 success；`a2b44b0` 的唯一失败是测试脚本按空标题找窗口——PowerShell 把 `$null` 传成空串——已在测试中修正，编辑器本身未改）。`--selftest-runtime` 新增的编辑器保存冲突用例与实现同时编写，未先观察到 RED。端到端测试方式：在 `0d32f56`/`e23cef0` 上，Windows PowerShell 5.1 的托管 UI Automation 在 CI 上把这些 Win32 控件全部识别为通用 Pane（无列表项、无 Invoke/Value/SelectionItem 模式），因此 `test-profiles-e2e.ps1` 改为与仓库其他 E2E 脚本相同的窗口消息驱动（WM_COMMAND、WM_SETTEXT/WM_GETTEXT、LB_GETCOUNT、向弹出菜单投递方向键/回车），job 更名为 “Profile editor end-to-end test”。这不是 UIA 测试；读屏/UIA 可访问性仍未验证。
- 第 3 步（设置窗口摘要与入口、缺失模型不替换、发现/连接测试版本守卫）：[实证行为 RED](https://github.com/kanoha222/typetide/actions/runs/40642378955)，commit `a3f8ccd`（`7a60723` 先推送，因测试里两个字符串字面量断行导致主程序编译失败，只有纯函数 job 是有效 RED；修正测试后重跑），build 成功：纯函数 [job `122663245042`](https://github.com/kanoha222/typetide/actions/runs/40642378955/job/122663245042) 18 failure(s)、运行时 mock [job `122663245027`](https://github.com/kanoha222/typetide/actions/runs/40642378955/job/122663245027) 11 failure(s)（发现结果替换了缺失模型并保存、预加载了缺失模型、连接测试未实现）、编辑器端到端 [job `122663245053`](https://github.com/kanoha222/typetide/actions/runs/40642378955/job/122663245053) 17 failure(s)（无摘要、旧控件仍在、无管理入口）。实现于 commit `d8793f7` [实证 GREEN](https://github.com/kanoha222/typetide/actions/runs/40644264307)：纯函数 [job `122669301867`](https://github.com/kanoha222/typetide/actions/runs/40644264307/job/122669301867) 0 failure(s)、运行时 mock [job `122669301899`](https://github.com/kanoha222/typetide/actions/runs/40644264307/job/122669301899) 全部通过、编辑器端到端 [job `122669302001`](https://github.com/kanoha222/typetide/actions/runs/40644264307/job/122669302001) 全部通过，9 个 check-run 全部 success。
- 第 3 步实现：设置窗口 Translation 页改为三个入口的摘要（profile、backend 与模型、本地/网络/云端、模型未安装提示、连接测试结果）与 “Manage profiles…”，保留内置模型下载、Ollama 地址与已装模型、云端 provider/Base URL/API key；Languages 页改为各入口的语言/方向/风格摘要；Behavior 页移除风格选择；快捷键页改称 Shortcut 1/2。旧的 backend 单选、Ollama/API 模型、语言/方向/风格控件已删除（字段仍保留在 settings.json 供迁移）。`ollamamodels::ApplyResolvedModels`/`EnsureValidDefault` 已删除：发现结果只用于预加载已绑定且已安装的模型，从不替换或保存；预加载由代次计数取消过期的批次。“Test bound models” 对每个已绑定的 backend + 模型各测一次（`translator::CheckConnectionAsync`，固定合成句、每次唯一、不命中缓存）。发现结果按 Ollama 端点版本、连接测试按连接 epoch 与绑定集合经 `profilestatus::ResultGuard` 丢弃过期结果。编辑器保存与托盘绑定后通知设置窗口刷新。独立审查后的修正：连接测试不读写翻译缓存、不记翻译诊断；设置窗口填好控件后才显示；连接或绑定变化后此前的测试通过不再计入首次启动完成。已知未处理：首次启动时应用与设置窗口各做一次发现，同一模型可能被预热两次（无害）。
- Phase 5 覆盖率工具（用户选择）：CI 运行器上从 OpenCppCoverage 官方 GitHub release 下载安装包，运行前核对固定的 SHA-256；本机不安装任何工具。

## Phase 5：验证与证据（进行中）

- 触发路径端到端（job `trigger-path-e2e`，[commit `01e78cd` 全部通过](https://github.com/kanoha222/typetide/actions/runs/40816512097/job/123258733455)）：真实 TypeTide.exe + 注入全局快捷键 + WinForms/WPF 宿主 + 本机 mock（Ollama，三个 profile 各用不同 mock 模型，按请求记录判断每次触发执行了哪个 profile）。覆盖快捷键 1 弹窗与弹窗 Replace（剪贴板还原）、快捷键 2 直接替换且 custom 输出逐字节一致、custom 提示词渲染进 system 而选中文字原样发送、两个槽位动作互换与相同动作时仍按物理槽位选 profile、清空的快捷键不触发、划词入口执行 Selection 绑定（WPF 宿主）、托盘三个绑定子菜单列出全部 profile 并勾选当前绑定、托盘改绑后下一次触发立即生效；并在 CI 上首次运行既有的 `test-e2e.ps1`（WinForms）与 `test-e2e-wpf.ps1`（WPF）冒烟测试。首次运行（`73d8741`）划词与托盘改绑失败：前者是测试只注入了鼠标抬起，后者在下一次运行（`1deb187`）未复现，按偶发处理并保留诊断输出。
- 已知产品限制（非本 PR 引入）：WinForms TextBox 不提供 UIA TextPattern（调试日志 `tp=0`），快捷键取词在其中走 Ctrl+C 兜底，而划词入口只用 UIA、从不对鼠标抬起发送 Ctrl+C，因此在此类控件中不会触发。
- 实测覆盖率（job `line-coverage`，[run `40816042368`](https://github.com/kanoha222/typetide/actions/runs/40816042368/job/123257210179)，commit `1deb187`）：OpenCppCoverage 0.9.9.0 安装包在运行前核对固定 SHA-256 与 SHA-512 首尾（取自 Chocolatey 对同一资产公布的值），Debug + PDB 构建四个独立测试目标与 TypeTide.exe，在覆盖率下运行全部自检（含对 mock 的 `--selftest-runtime`），合并为 Cobertura，由 `windows/scripts/coverage-report.py` 统计：**本 PR 改动的核心行 1936/2158 = 89.7%**（门槛 ≥80%，已达到；核心 = 相对 `origin/main` 改动的 `windows/src` 非 UI、非自检源文件，只计编译器生成代码的行，各测试二进制的命中合并）；核心文件全部行 2360/2855 = 82.7%；项目基线（不含自检文件）2686/6346 = 42.3%；UI 文件 35/2725 = 1.3%（UI 由端到端 job 驱动，端到端不在覆盖率下运行）。最低的核心文件：SelectionCapture.cpp 0%（只有端到端可达）、TextReplacer.cpp 38%、Credentials.cpp 47%（测试从不访问真实凭据管理器）、OllamaModels.cpp 69%、ProfileBinding.cpp 79%。有改动却未被任何覆盖二进制编译的核心源文件会使门槛失败，而不是被当作已覆盖。
- 未在覆盖率下运行：端到端 job（UI、取词、粘贴路径）。未验证：读屏/UIA 可访问性、真实 provider 与真实凭据管理器、macOS 功能适配。
- 最终审查（两个独立审查，只读，审查对象 `01e78cd`）：安全审查 0 CRITICAL、1 HIGH、5 MEDIUM、9 LOW；正确性审查 0 CRITICAL、5 MEDIUM、7 LOW。已修正（回归测试与修正同时编写，未先观察到 RED）：粘贴守卫不再在取词窗口未知时放行，并额外核对取词时有焦点的控件（同一窗口换了输入框也放弃）；前台窗口未知时不发送 Ctrl+C；剪贴板无法保存或没有写入我们的文本、或在 Ctrl+V 前被他人替换时放弃替换（不再把用户原剪贴板粘贴出去），无法读取的快照不再在还原时清空剪贴板；Base URL 与 API key 在离开输入框（或关闭窗口）时才提交，key 去首尾空白；旧版明文 API key 在启动时迁入凭据存储并重写 settings.json（`Settings::migrateLegacyPlaintextKey`）；Ollama 发现与预加载使用与翻译请求相同的地址规则（`execution::OllamaBase`），发现不跟随重定向，没有绑定 Ollama 时启动不做发现；流式响应总量上限 16 MiB；API key 含空格/换行/控制字符/非 ASCII 时拒绝发送（`PrepareStatus::KeyInvalid`）；provider 错误文本截断到 300 字符并去掉控制字符；本机（loopback）请求不走代理；运行时自检结束时还原剪贴板；旧版设置里空白或无效的模型迁移为旧版默认模型，而不是让整个文件变成只读；排除应用的数量/长度上限在界面里检查；设置只读时在 General 页说明原因；快捷键走剪贴板兜底取词前先让旧会话失效（期间完成的旧翻译不能再粘贴）；退出后丢弃仍在进行的取词；编辑器“固定文本测试”不读写缓存（`translator::StreamUncached`）；预加载在 Ollama 地址变化时停止，并对拒绝 `think` 的模型重试；托盘改绑后，没有未保存修改的已打开编辑器重新载入。待用户决定：远程明文 HTTP 的 Ollama（选中文字与提示词以明文发送，且直接替换会把可能被篡改的响应粘贴进前台应用）；custom 输出在“替换”时是否保留结尾换行（已批准的设计要求逐字节一致，但粘贴进终端会执行该行）。已知、未在本轮修正：`.tmp` 残留会阻止之后的保存（已列入集成门槛）；API key 未与端点绑定（切换 provider 预设后旧 key 会发往新端点）；引用的凭据缺失时整个设置只读（设计如此，现已在界面说明）；设置窗口仍是“即改即存”的 draft 模型（已列入集成门槛）；编辑后提示词的 LF 会变成 CRLF；设置窗口关闭时队列里尚未处理的消息负载会泄漏；严格 JSON 会丢弃含非法 UTF-8 的流式行；每次提交在 UI 线程上刷盘；JSON 重复键后者覆盖（仅旧版导入）。

## 验证入口与剩余门槛

- [工作流](../../../.github/workflows/translation-profiles.yml) 分别运行 `ProfileCoreSelfTest`、`NativeStorageSelfTest`、`NativeTransactionSelfTest` 与新增的 `TranslationExecutionSelfTest`，保留真实失败退出码与日志构件。
- 本批没有本地 configure/build/run、工具安装/下载、模型/provider 调用、真实 CM 操作或 CI 轮询。execution 目标须在主会话独立审查并发布后，由云端给出实际 RED/GREEN；不得以源码审阅代替执行证据。
- 文件替换与凭据存储跨子系统，不声称断电时具备绝对原子性；崩溃可能留下无引用记录。本实现不枚举、不做启动扫删；只能清理有实际来源证据的记录。
- 后续仍需 Settings/UI/provider 完整集成、完整旧/新基线、mock/UIA、macOS 功能适配与完整验收及实测覆盖率 ≥80%（未测不得勾选通过）。公开日志仅含合成数据，不包含私人机器诊断、密钥、ref/hash 或选中文字。

## 集成门槛（待办，非已完成功能）

- 公开 `Settings` 字段仍是旧版可编辑 draft。翻译执行已改为冻结 committed snapshot（不再读 draft，GREEN `54f0e9a`）；设置窗口的连接测试也只用已提交的值。保存失败时可能保留未提交 draft，与旧行为一致。
- 功能验收前，UI 必须使用 candidate commit 并在提交后重新同步控件、处理 error + warning / 只读状态。
- `.tmp` 的 `CREATE_NEW` 冲突保护未知文件，但需要明确的恢复 UX，不能静默删除非自有临时文件。
- `TYPETIDE_DATA_DIR` 下的进程内存凭据仅用于测试，不是持久化的生产路径。
- 以上均为后续任务；历史 RED 21 failure(s) 与运行时 GREEN 待验证状态不变。
