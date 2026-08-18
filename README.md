# lw.Web2Android

`lw.Web2Android` 的目标是把本地 Web 项目或在线 URL 转换为可安装的 Android APK。v0.2.0 可以从官方源初始化锁定的最小工具链，最终用户无需安装 Android Studio、Gradle、完整 Android SDK 或完整 JDK。

## 当前进度

v0.1.0 的 M0–M6 已完成并正式发布。项目当前进入 **v0.2.0 / M7：End-User Toolchain**，重点解决首次下载后的零 Android 环境构建体验。

M0 最小流水线会：

1. 用 `javac` 和 D8 将固定命名空间的 Java Runtime 编译成 `classes.dex`；
2. 用 AAPT2 动态编译 Manifest、资源和 Web assets；
3. 将预编译的 DEX 注入资源 APK；
4. 依次执行 `zipalign`、`apksigner sign` 和 `apksigner verify`；
5. 生成可安装的 `sample-debug.apk` 和 SHA-256 文件。

M0 使用一次性的测试签名，仅用于验证 APK 组装路线。正式流水线已实现“每个 Package Name 独立签名 + DPAPI 加密私钥 + 密码保护的 PKCS#12 备份”。

## 在 GitHub Actions 中验证

推送代码或手动运行唯一的 **lw.Web2Android CI** workflow。Runtime、最小工具链、Packer、GUI、签名和发行元数据验证全部成功后，只会上传一个 `lw-Web2Android-v0.2.0-windows-x64` Artifact：

```text
lw-Web2Android-v0.2.0-windows-x64/
├── bin/
│   ├── lw.Web2Android.exe
│   └── lw.Web2Android.GUI.exe
├── toolchain/
│   ├── jre/                 # 可再分发的 Temurin 17 JRE
│   ├── runtime/
│   │   ├── classes.dex
│   │   └── metadata.json
│   └── runtime-v1.zip
├── samples/
│   ├── m0/
│   │   ├── sample-debug.apk
│   │   └── sample-debug.apk.sha256
│   └── v0.2/
│       ├── lw-Web2Android-Hello-1.0.0-android.apk
│       ├── lw-Web2Android-Hello-1.0.0-android.apk.sha256
│       ├── lw-Web2Android-Hello-1.0.0-android.release.json
│       ├── lw-Web2Android-Hello-1.0.0-android-RELEASE.md
│       └── ...Remote 对应文件
├── docs/
├── tools/
│   ├── install-minimal-toolchain.ps1
│   └── assemble-minimal-toolchain.ps1
├── LICENSE
├── toolchain.lock.json
├── release.json
├── RELEASE.md
└── SHA256SUMS.txt
```

下载一次即可取得本轮 CI 的全部产物。v0.2 样例由 CI 使用扁平最小工具链实际构建，用于同时验证 local/remote、签名、升级身份、发行追溯和零环境工具链解析路线。

统一 CI 会缓存 `toolchain.lock.json` 指定的 Android Platform、Build Tools、Gradle 依赖与 Gradle Build Cache。签名身份、APK、Runtime 最终产物及完整构建目录不会进入缓存。

## v0.2.0 首次使用

解压公开发行包并双击 GUI。发行包已经包含可再分发的 Temurin 17 JRE 与 Runtime。工具链未准备好时，点击“初始化工具链”，阅读并接受 Android SDK License 后，程序会从 Android 官方源下载剩余的锁定组件，校验 SHA-256，并安装到当前应用目录：

```text
lw-Web2Android-v0.2.0-windows-x64\toolchain\
├── aapt2.exe
├── zipalign.exe
├── android.jar
├── apksigner\apksigner.jar
├── jre\
├── runtime\
│   ├── classes.dex
│   └── metadata.json
└── metadata.json
```

也可以在 PowerShell 中执行：

```powershell
./tools/install-minimal-toolchain.ps1 -AcceptAndroidSdkLicense
```

初始化器只从 `toolchain.lock.json` 指定的来源下载，并在解压前验证 SHA-256。下载过程仅使用系统临时目录并在结束时清理，不创建持久缓存；最终工具链永久保存在当前应用目录。仍可通过 `--android-sdk`、`--java-home`、`ANDROID_SDK_ROOT` 和 `JAVA_HOME` 使用开发环境。

## 本机私有完整发行包

Android SDK 受其独立许可约束，因此公开 GitHub Release 不直接重新分发 Google SDK 组件。完成本机工具链初始化和 C++ Release 构建后，可为自己生成包含 `toolchain/` 的私有完整包：

```powershell
./tools/package-local-release.ps1
```

输出为 `build/releases/lw-Web2Android-v0.2.0-windows-x64-complete-private.zip`，同时生成 ZIP SHA-256。该私有包不会被 CI 上传；重新分发前必须自行确认相关组件许可。

## 架构约束

- Windows Packer 使用 C++17；
- Android Runtime 使用 Java，并固定命名空间为 `com.lw.web2android.runtime`；
- Runtime 不引用动态生成的应用 `R` 类；
- APK 资源由 AAPT2 动态生成，Runtime DEX 编译一次后重复注入；
- 本地 Web 正式实现将使用 `WebViewAssetLoader`；
- 不提供 JavaScript ↔ Native Bridge。

M1 Runtime 已实现首版 `WebViewAssetLoader`、配置读取及 local/remote 加载，并已纳入统一 CI。

## M5 原生 Windows GUI

`lw.Web2Android.GUI.exe` 使用 C++17 和原生 Win32 构建，视觉与交互风格参考同系列项目 [`lw.Web2App`](https://github.com/lxw112190/lw.Web2App)：浅蓝页头、白色圆角卡片、蓝色主按钮、独立状态区以及 Per-Monitor V2 高 DPI 布局。

GUI 支持本地网页目录和远程 HTTPS 地址，可设置应用名称、Package Name、版本、方向、全屏与输出目录。界面只负责创建 `ProjectConfig`，实际生成仍由稳定的 `BuildPipeline` 在工作线程完成，并实时显示 15 个构建阶段；CLI 功能与行为保持不变。

双击运行：

```text
bin\lw.Web2Android.GUI.exe
```

GUI 优先使用当前应用目录中的 `toolchain/`，并兼容 `ANDROID_SDK_ROOT` 和 `JAVA_HOME`。下载前必须由用户显式确认 Android SDK License。

## M6 发行与版本追溯

未设置 `outputFile` 时，Packer 使用 `<AppName>-<Version>-android.apk` 标准文件名，并过滤 Windows 禁止字符。每次成功构建会同时生成：

```text
<APK>.sha256
<APK名称>.release.json
<APK名称>-RELEASE.md
```

发行元数据记录 App Name、Package Name、Version Name/Code、APK SHA-256、签名证书 SHA-256、Runtime Version、Toolchain Version 和 UTC 构建时间。CI 会独立核对这些值与实际 APK、`toolchain.lock.json` 是否一致。

`v*` 标签在完整构建通过后会自动创建 GitHub Release，上传 `lw-Web2Android-v0.2.0-windows-x64.zip` 及其 SHA-256 文件。建议先让 `main` 分支 CI 成功，再创建并推送标签：

```bash
git tag -a v0.2.0 -m "lw.Web2Android v0.2.0"
git push origin v0.2.0
```

## Packer CLI、自动签名与身份备份

Windows C++17 Core 当前流水线负责：

1. 校验 `project.json`、本地入口或远程 URL；
2. 生成 `AndroidManifest.xml`、Android 资源和 `assets/lw-config.json`；
3. 用锁定版本 AAPT2 编译并链接资源 APK；
4. 用内置 ZIP 组装器注入 `runtime-dist/runtime-v1/classes.dex`；
5. 用 `zipalign` 生成并验证 aligned APK；
6. 为每个 Package Name 创建或复用独立的 RSA 3072 签名身份；
7. 使用 Windows DPAPI 保存加密后的 PKCS#8 私钥；
8. 使用 `apksigner` 签名、验证并生成 APK SHA-256；
9. 生成 JSON 与 Markdown 发行元数据。

构建和命令行用法参见 `packer/README.md`。统一 CI 依次完成 Runtime 编译、M0 验证、Packer 单元测试、local/remote 正式签名 APK 集成验证，最后打包成一个下载项。

默认签名身份保存在：

```text
%LOCALAPPDATA%\lw.Web2Android\keys\<Package Name>\
```

同一 Package Name 后续构建必须复用该身份，否则 Android 无法将新 APK 作为原应用升级。可使用以下命令查看身份并导出密码保护的标准 PKCS#12 备份：

```powershell
lw.Web2Android.exe signing info com.example.app
lw.Web2Android.exe signing export com.example.app D:\backup\com.example.app.pfx
```

导出密码通过关闭回显的交互式控制台输入，不会出现在命令行或日志中。备份文件默认不会覆盖同名文件；请将备份与密码分开妥善保管。

## 联系与支持

- 作者：天天代码码天天
- QQ：819069052
- QQ Group：C# 人工智能实践（群号：758616458）

## 赞助维护

如果项目对你有帮助，可以扫码支持项目的持续维护：

<img src="assets/sponsor.jpg" alt="微信赞助二维码" width="360">

## License

项目源代码采用 [MIT License](LICENSE)，版权所有 © 2026 天天代码码天天。Android SDK、Eclipse Temurin 与 AndroidX 等第三方组件继续适用各自许可证，详见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。
