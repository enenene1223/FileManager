# 构建与回归测试

默认 `BUILD_TESTING=OFF`，仅生成 FileManager 主程序及 Qt 必需的自动生成辅助目标，不编译测试程序。
AI 设置源码直接编入应用，不再创建独立的 ai_service_settings 库目标。
所有测试入口源码和测试目标定义均位于 tests/；主程序源码仍从项目根目录引用。

## 日常开发

在 Qt Creator 的「项目 / 构建设置 / CMake」中将 BUILD_TESTING 设为 OFF，然后重新配置并构建。
首次配置默认关闭；已有构建目录若曾开启测试，需显式关闭该缓存选项。
命令行（已有 Qt、MinGW、OpenSSL 配置的构建目录）：

```sh
cmake -S . -B build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug -DBUILD_TESTING=OFF
cmake --build build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug --parallel 4
```

## 回归测试

建议使用独立构建目录以免影响日常配置。首次配置须使用本机 Qt Kit、编译器与 OpenSSL 路径。

```sh
cmake -S . -B build/CMakeCleanup_Testing -DBUILD_TESTING=ON
cmake --build build/CMakeCleanup_Testing --parallel 4
ctest --test-dir build/CMakeCleanup_Testing --output-on-failure
```

- core_regression：文件系统、复制、移动、撤销与持久化等核心回归。
- crypto_regression：AES-GCM 加解密和异常输入回归。
- acceptance_probe：搜索、归类、导航、权限、界面等综合回归。
- ai_settings_regression：凭据存储与设置界面回归，使用独立临时凭据。
- release_startup：复用 FileManager 的 --smoke-test，不生成新程序。

四个现有测试程序覆盖不同层次，保留以减少改动；后续小功能测试优先扩展已有测试。
测试数据仍隔离于构建目录 acceptance-data。Windows 凭据测试需在有凭据管理器访问权限的用户会话中运行。
测试程序只生成在构建目录中，不安装到 Release。安装与部署规则保持不变。
## 本地双语搜索回归

综合回归 acceptance_probe 复用原目标，覆盖中英文双向别名（游戏/game/games/gaming）、复合词（工作报告/work_report）、空格分隔多词 AND、原词排序优先、扩展英文单词边界、未知词保留、单字回退、正文开关、范围和属性过滤、继承隐私权限及索引/遍历结果与分数一致性。

搜索扩展使用 LocalSearchTerms.h 中的窄同义词组，不将同一主题的所有词互换，也不进行联网翻译。名称与可选 TXT 正文复用同一匹配规则。原词直接匹配优先于扩展匹配；每层中名称优先于正文（按关键词累加相关度），用户选择其他排序时仍尊重其选择。结果悬停显示匹配词及名称/正文来源，不展示正文片段。
## 只读 AI 搜索 Agent

DeepSeek 辅助的「智能搜索」使用官方 Tool Calls 多轮协议：[DeepSeek Tool Calls](https://api-docs.deepseek.com/guides/tool_calls/)。不再将自然语言搜索局限于固定的筛选条件 JSON。

1. 智能搜索默认覆盖整个项目，移除范围控件和每次搜索前的确认弹窗；界面说明将发送搜索语句及未加密项目的名称、路径、大小、创建时间与修改时间。加密项目和正文仍不上传。
2. Agent 自行调用 query_files、list_directory、get_metadata，最后通过 finish_search 返回选中的文件 id 与依据；本版本不提供正文读取、移动、删除或 Windows 命令接口。
3. finish_search 的 files 仅包含最终匹配项，不能包含比较后排除的候选；创建时间与修改时间分别返回，分页会说明总数、后续页和时间并列。
4. 完成后可点「在文件列表中显示结果」，双击沿用原有结果定位。显示前重新验证文件存在、权限、路径、大小与时间；失效则保留原列表并提示重试。
5. 加密项目及其后代即使已在本地解锁，也不上传元数据；正文始终不包含在 Agent 快照中。联网归类和 TXT 摘要沿用原确认流程。
6. 每次任务上限六轮请求、十二个工具调用、总等待90秒；单轮20秒，可取消。最多返回30项，查询单页最多100项，任务范围最多一万项。超过一万项时不发送请求，提示使用本地搜索，不静默忽略超出的文件。任务只保留本窗口本次上下文，不支持跨任务追问。

继续复用 acceptance_probe 与 ai_settings_regression，不新增可执行目标。覆盖模拟多轮查询/时间比较/单项选择、分页与并列、无效或捏造id、调用上限、响应大小、HTTP/超时/取消，以及真实虚拟文件系统的隐私过滤、失效结果拒绝、结果定位与离线搜索回退。

可手动在已有 Qt/SSL 运行环境执行 `ai_settings_regression --agent-live-smoke`，使用当前用户已配置的密钥与两条虚构文件元数据验证真实工具调用。该操作会产生少量 API 请求，不读用户文件、不输出密钥；没有配置密钥时跳过。不纳入默认 CTest，以免日常回归联网或计费。

2026-10-15：真实 DeepSeek 工具调用合成测试通过，模型先查询再比较创建时间，仅选中较新的点名册1，即使旧文件修改时间更晚。AI 判断仍可能不准确，应核对界面给出的时间和依据；不等于任意任务、真实文件全集或正文语义检索均已验证。