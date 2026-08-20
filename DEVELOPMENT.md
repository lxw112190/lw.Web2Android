# Visual Studio 2022 开发说明

完整开发包可直接复制到另一台 Windows 10/11 x64 电脑。包内包含 C++ 源码、离线 `spdlog` 依赖、预编译 Android Runtime v6、最小 Android 打包工具链、Java 17 JRE，以及已经编译好的 GUI/CLI。

## 直接运行

双击根目录的 `lw.Web2Android.GUI.exe`。`toolchain/` 已经就绪，不需要下载 Android SDK、安装 Android Studio或配置 `JAVA_HOME`。

工具链包含 Android SDK 许可所覆盖的组件，仅供已接受相应许可的用户在许可范围内使用。不要把完整私有开发包上传为公开 Release。

## 使用 Visual Studio 2022 编译

1. 安装 Visual Studio 2022，并选择“使用 C++ 的桌面开发”；
2. 双击根目录的 `lw.Web2Android.sln`；
3. 在工具栏选择 `Release` 和 `x64`；
4. 点击“生成 → 生成解决方案”。

这是原生 VS2022 解决方案，不调用 PowerShell、CMake 或 Ninja。解决方案包含：

```text
lw.Web2Android.sln
├── lw.Packer.Core       C++17 静态库
├── lw.Web2Android       命令行程序
├── lw.Web2Android.GUI   Win32 GUI（默认启动项目）
└── lw.Packer.Tests      本机测试程序
```

完整私有开发包已经在 `.deps/spdlog-src/` 中准备好头文件依赖，编译不联网。各工程使用相对路径，并显式禁用上级目录的 `Directory.Build.props`、`Directory.Build.targets` 和全局 vcpkg 集成，复制整个目录后不需要重新生成 `.sln`。

建议解压到较短的路径，例如 `D:\src\lw.Web2Android`，避免旧版工具链的中间文件路径超过 Windows 路径长度限制。

Release 输出位于：

```text
build/vs2022-native/x64/Release/
├── lw.Web2Android.exe
├── lw.Web2Android.GUI.exe
└── lw_packer_tests.exe
```

仓库仍保留 CMake 构建给 CI 和命令行开发者，但它不是完整私有开发包使用 Visual Studio 编译的前置条件：

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-release
ctest --preset vs2022-release-tests
```

## 构建范围

Visual Studio 工程直接编译 C++17 Packer、Win32 GUI 和本机测试。APK 打包直接复用 `toolchain/runtime/` 中经过 CI 生成并校验的 Runtime v6，因此仅修改 Packer/GUI 时不需要重新编译 Android Java Runtime。

如果修改了 `runtime/` 下的 Android Java 源码，仍应通过项目 GitHub Actions 重新生成 Runtime Bundle，再替换 `toolchain/runtime/classes.dex` 和 `toolchain/runtime/metadata.json`。
