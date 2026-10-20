# 翻译配置：环境与进度状态

状态日期：2026-10-20。**纯核心编解码与领域校验在 commit df2fc7c 达成完整 GREEN（4 项 CI 检查全部成功）；现已完成原生设置存储黑盒保护用例与凭证双重隔离测试脚手架登记，原生存储 RED 待云端独立测试实际跑验证。** [文档索引](README.md) · [待办清单](task_list.md)

## 当前证据

- 基础设施 [PR #1](https://github.com/kanoha222/typetide/pull/1) 已合并。当前为 [draft PR #2](https://github.com/kanoha222/typetide/pull/2)，分支 `feat/translation-profiles-implementation`。
- **历史演进记录**：
  - JSON 阶段 RED（commit `ea90e73`，30 failures）已于 commit `439b9ff` 实证 GREEN；
  - 完整 Profile 脚手架 RED（commit `708a593`，job `117972461843`，112 failures）实证了桩阶段的完整领域契约 RED；
  - 针对性修复与回归追加（commit `df2fc7c`，修复 Encode fail-open 导致 legacy 回退根因）。
- **纯核心完整 GREEN 实际证据（commit `df2fc7c`）**：
  - 云端 CI [run 39356274299](https://github.com/kanoha222/typetide/actions/runs/39356274299) 4 项 check-runs 全部 success：
    - [Profile core self-test](https://github.com/kanoha222/typetide/actions/runs/39356274299/job/118411675231) 运行 `ProfileCoreSelfTest.exe` 报告 0 failure(s)，完整通过；
    - Windows build + self-test（job `118411674966`）通过；
    - macOS unit + end-to-end tests（job `118411674956`）通过；
    - Brand consistency（job `118411674587`）通过。
- **原生存储 RED 准备证据（当前待实际跑）**：
  - 测试目标：`NativeStorageSelfTest.exe`，源码 `windows/tests/NativeStorageSelfTest.cpp`；
  - 凭证测试替身：`windows/tests/TestCredentialsDouble.h`、`windows/tests/TestCredentialsDouble.cpp`，实现链接期隔离，计数意外调用并在测试末尾断言为 0，杜绝 WinCred 依赖与环境失败导致的假绿；
  - 黑盒保护用例：覆盖未来 schema 3、损坏 JSON、零字节文件、超大文件（>1 MiB）、读锁 IO 错误下的拒 save 且保 bytes 不变，以及缺少文件时的初始安装允许 save 对照；
  - 隔离保障：基于 `RUNNER_TEMP` 独立目录，动态捕获/恢复环境变量（无 MAX_PATH 截断），并在调用前后严格校验目录前缀；若环境设置失败则以 infra 退出码终止，防止伪造成行为 RED；
  - 构建与 CI 登记：在 `windows/tests/CMakeLists.txt` 中添加目标，在 `.github/workflows/translation-profiles.yml` 中登记独立 job `native-storage`；
  - 生产代码边界：未修改任何生产源码、头文件、根 CMakeLists.txt 或枚举类型；
  - 状态：测试已登记，待云端复跑验证实际执行表现。

## 当前 CI 入口与边界

- [工作流](../../../.github/workflows/translation-profiles.yml) 包含两个独立 Windows 任务：
  - `profile-core`：运行完整纯核心契约测试套件（`ProfileCoreSelfTest.exe`，已实证 GREEN）；
  - `native-storage`：运行原生设置存储黑盒保护测试套件（`NativeStorageSelfTest.exe`，待实际跑实证 RED）。
- 失败不掩盖（`exit $LASTEXITCODE`），日志均作为构件留存。
- draft PR #2 已启用 auto-fix；不合并、不发版。不连接真实网络 provider、不执行模型、不注入密钥、不采集选中文字；公开日志仅使用合成数据，不包含私人机器诊断。

## 后续门槛（按顺序）

1. 云端 Windows 独立测试实际运行 `native-storage`，实证原生存储黑盒行为 RED。
2. 在验证原生存储 RED 后，开展最小持久化锁定与配置提交实现，使原生存储走向 GREEN。
3. 补齐 macOS 最终状态、完整旧/新基线、mock/UIA、实测覆盖率和代码/安全审查；未执行项或不足如实保留，达到各项验收门槛后才更新任务状态。
