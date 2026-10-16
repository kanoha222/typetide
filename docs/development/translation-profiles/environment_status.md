# 翻译配置：环境与进度状态

状态日期：2026-10-16。**业务实现前暂停，等待云端实际行为 RED；没有任何应用功能完成。** [文档索引](README.md) · [待办清单](task_list.md)

## 当前证据

- 设计文档阶段已完成，声明/测试文件已准备并经过静态审查；这不是 C++ 编译、运行时验证、RED/GREEN 或覆盖率证据。
- 本地工具链不可用。本地获取的 Microsoft 清单未通过官方哈希核验，不得用于准备工具链；本次不访问或使用本地工具链下载目录，也不重试安装、下载或工具执行。
- 用户已选择 GitHub Windows CI，使用 Windows runner 预装的 MSVC、Windows SDK 与 CMake，不另行安装工具。
- 独立核心测试项目与工作流已在本地准备，**尚未发布或运行**。实际 CI 状态须在协调者完成独立审查并发布分支/draft PR 后据实填写；当前不宣称任何测试已失败或通过。
- 24 项实现/验证任务仍全部待办；功能实现、旧测试基线、provider mock、UIA E2E 与实测覆盖率均未完成。

## 本次 CI 入口与边界

- [工作流](../../../.github/workflows/translation-profiles.yml) 在 `feat/translation-profiles` 推送及相关 PR 源码/测试路径变更时触发，仅有仓库内容读取权限、并发取消与有界超时。
- [独立 CMake 项目](../../../windows/tests/CMakeLists.txt) 只构建控制台 `ProfileCoreSelfTest`，初始 `PROFILE_CORE_JSON_ONLY=ON`，使用现有 `Json.cpp` 与已准备的 `ProfileSelfTest.cpp`。不修改应用主构建或生产源码。
- 工作流分别保留 configure/build/self-test 日志，即使失败也上传已有日志；不忽略非零退出码，也不把预期失败改成成功。
- 当前预计现有 JSON 解析器会触发严格边界断言失败，但**实际行为 RED 尚未观察到**。配置、编译或工具链错误不得当作行为 RED。
- 不连接真实网络 provider，不执行模型，不注入密钥，不采集选中文字；公开文件和日志只能包含测试基础设施与合成测试数据，不包含私人机器诊断。
- 既有主 CI 在 PR 上继续负责原有 Windows 应用 self-test 与 macOS 检查；本工作流不重复整套基线，也不代替尚未执行的 mock/UIA/覆盖率验证。

## 恢复门槛（按顺序）

1. 协调者完成代码/安全独立审查，再按用户授权发布限定文件并创建 draft PR；不合并、不发版，不上传密钥、用户文本或私人机器诊断。
2. 在 Windows runner 上成功配置并构建独立测试，运行控制台程序，检查真实断言与非零退出码；只有目标行为的预期失败才记为 RED。
3. 核心负责人依据实际 RED 进行最小实现，逐步将 profile/store/Language 等实现源码接入独立目标，并在适当阶段关闭 JSON-only，验证完整核心测试的 GREEN。
4. 执行旧/新基线、mock/UIA、实测覆盖率和代码/安全审查；未执行项或不足如实保留，达到验收门槛后再更新任务状态。
