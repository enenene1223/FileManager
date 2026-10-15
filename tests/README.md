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