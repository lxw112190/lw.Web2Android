# Packer CLI 与 GUI（v0.2.0）

Packer 是 Windows C++17 程序。它读取 `project.json`，动态生成 Manifest、资源和 Runtime 配置，通过锁定版本的 AAPT2 生成资源 APK，在内部注入预编译 Runtime DEX，执行 `zipalign` 后使用 Package 独立身份签名并验证 APK。

## 构建

```powershell
cmake -S packer -B build/packer -A x64 -DBUILD_TESTING=ON
cmake --build build/packer --config Release
ctest --test-dir build/packer -C Release --output-on-failure
```

构建后会生成：

```text
lw.Web2Android.exe       CLI
lw.Web2Android.GUI.exe   原生 Win32 GUI
```

GUI 参考 `lw.Web2App` 的设计语言，支持 Per-Monitor V2 高 DPI、后台工作线程和 15 步构建状态。GUI 只把表单映射为 `ProjectConfig`，所有 APK 逻辑仍由 `BuildPipeline` 完成。

首次使用可在 GUI 点击“初始化工具链”。公开包已包含 Temurin 17 JRE 与 Runtime；用户确认 Android SDK License 后，初始化器从官方源下载并校验锁定的 Android 组件，安装到当前应用目录的 `toolchain/`。下载临时文件会自动清理，后续构建直接复用，无需系统 Android SDK 或 JAVA_HOME。

## 使用

```powershell
build/packer/Release/lw.Web2Android.exe validate project.json
build/packer/Release/lw.Web2Android.exe build project.json
build/packer/Release/lw.Web2Android.exe signing info com.example.myapp
build/packer/Release/lw.Web2Android.exe signing export com.example.myapp D:\backup\myapp.pfx
```

可用参数：

```text
--android-sdk <directory>  覆盖应用目录中的最小工具链或 ANDROID_SDK_ROOT
--java-home <directory>    覆盖 JAVA_HOME
--runtime <directory>      覆盖 project.json 中的 Runtime Bundle 目录
--keys-dir <directory>     覆盖 DPAPI 签名身份存储目录
--keep-work-dir            保留中间目录用于诊断
```

默认签名身份位于 `%LOCALAPPDATA%\lw.Web2Android\keys\<Package Name>\`：

```text
signing.key.lw   DPAPI 加密的 PKCS#8 私钥
certificate.pem  X.509 自签名证书
metadata.json    Package、证书 SHA-256 与创建时间
```

私钥仅在调用 `apksigner` 时解密到隔离工作目录，签名结束后立即覆盖删除。相同 Package 会复用同一证书，不同 Package 不共享私钥。

## 打包日志

CLI 与 GUI 使用 spdlog 的同步 rotating sink 记录 15 个构建阶段、AAPT2/zipalign/apksigner 输出、结果摘要及异常：

```text
<发布包目录>\logs\packer.log
```

Packer 会在发布包当前目录自动创建 `logs` 文件夹；如果单独复制并运行 EXE，则在 EXE 所在目录创建。单文件最大 2 MiB，保留当前文件和 5 个轮转文件。日志初始化或写入失败不会阻断 APK 构建。GUI 构建失败时会在错误弹窗中显示日志路径；签名私钥与 PKCS#12 备份密码不会写入日志。

未在 `project.json` 中设置 `outputFile` 时，默认 APK 文件名为 `<AppName>-<Version>-android.apk`。每次构建同时输出 `<APK>.sha256`、`<APK名称>.release.json` 和 `<APK名称>-RELEASE.md`。发行元数据包含应用版本、APK 与证书 SHA-256、Runtime/Toolchain 版本和 UTC 构建时间。

`signing info` 只读取已有身份，不会自动创建新密钥。`signing export` 会在控制台关闭回显后要求输入并确认至少 8 个字符的密码，生成包含证书和私钥的标准 PKCS#12 备份；密码不会进入命令行或日志。目标文件已存在时命令会拒绝覆盖。请把备份和密码分开保管，丢失签名身份后将无法发布可覆盖安装的更新。

GitHub Actions 使用统一的 `lw.Web2Android CI` workflow。CI 会从已安装 SDK 组装扁平最小工具链，检出固定版本的 `wechat-article-formatter`，构建真实 React/Vite `dist` 并生成 Demo APK；Remote 模式仍作为内部集成测试。CLI、GUI、Runtime、初始化脚本、MIT License、Demo APK、发行元数据和总校验文件会合并到单个 `lw-Web2Android-v0.2.0-windows-x64` Artifact。公开发行包不重新分发 Android SDK。

## project.json

完整的本地模式示例见项目根目录 `README.md`，在线模式测试配置见 `samples/remote/project.json`。路径均相对于配置文件解析，HTTP 仅在 `allowHttp` 为 `true` 时允许。

本地项目的 `entry` 相对于 `source` 目录填写；打包时网页会放入 APK 的 `assets/www/`，生成的 `assets/lw-config.json` 会把入口转换为对应的 `www/<entry>` 资产路径。
