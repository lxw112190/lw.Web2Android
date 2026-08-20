# Visual Studio 2022 开发说明

完整开发包可直接复制到另一台 Windows 10/11 x64 电脑。包内包含 C++ 源码、离线 `spdlog` 依赖、预编译 Android Runtime v6、最小 Android 打包工具链、Java 17 JRE，以及已经编译好的 GUI/CLI。

## 直接运行

双击根目录的 `lw.Web2Android.GUI.exe`。`toolchain/` 已经就绪，不需要下载 Android SDK、安装 Android Studio或配置 `JAVA_HOME`。

工具链包含 Android SDK 许可所覆盖的组件，仅供已接受相应许可的用户在许可范围内使用。不要把完整私有开发包上传为公开 Release。

## 使用 Visual Studio 2022 编译

1. 安装 Visual Studio 2022，并选择“使用 C++ 的桌面开发”和“用于 Windows 的 C++ CMake 工具”；
2. 双击根目录的 `lw.Web2Android.sln`；
3. 在工具栏选择 `Release` 和 `x64`；
4. 点击“生成 → 生成解决方案”。

解决方案中的工程会调用包内的 CMake 预设，自动编译 CLI、GUI 和测试程序。所有源码都使用相对路径，整个目录复制到另一台电脑后不需要重新生成 `.sln`。

也可以在 Visual Studio 中选择“打开本地文件夹”，打开开发包根目录并使用 `Visual Studio 2022 x64` CMake 预设。

Visual Studio 使用根目录的 `CMakePresets.json` 自动生成当前电脑专用的构建目录，不依赖原打包电脑的绝对路径。C++ 构建使用 `.deps/spdlog.tar.gz`，配置阶段不需要联网下载第三方库。

构建入口会自动规整进程环境中可能重复的 `PATH` / `Path`，避免 MSBuild 因环境变量名称大小写重复而无法启动编译器。工程会优先使用 VS2022 自带的 CMake，避免误用 Python 包或其他软件写入 `Path` 的同名 CMake 启动器。完整构建过程记录在：

```text
logs/vs2022-build.log
```

日志达到 2 MiB 后自动轮转，最多保留 5 份历史文件。若 Visual Studio 只显示 `MSB3073` 或“已退出，代码为 1”，请查看或发送该日志；真正的 CMake、编译器和链接器错误会保留在其中。

建议解压到较短的路径，例如 `D:\src\lw.Web2Android`。避免在多层压缩包或过深的目录中直接编译，以免旧版 MSBuild 的中间文件路径超过 Windows 路径长度限制。

Release 输出位于：

```text
build/vs2022-x64/packer/Release/
├── lw.Web2Android.exe
├── lw.Web2Android.GUI.exe
└── lw_packer_tests.exe
```

也可以在“开发人员 PowerShell”中执行：

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-release
ctest --preset vs2022-release-tests
```

## 构建范围

Visual Studio 工程直接编译 C++17 Packer、Win32 GUI 和本机测试。APK 打包直接复用 `toolchain/runtime/` 中经过 CI 生成并校验的 Runtime v6，因此仅修改 Packer/GUI 时不需要重新编译 Android Java Runtime。

如果修改了 `runtime/` 下的 Android Java 源码，仍应通过项目 GitHub Actions 重新生成 Runtime Bundle，再替换 `toolchain/runtime/classes.dex` 和 `toolchain/runtime/metadata.json`。
