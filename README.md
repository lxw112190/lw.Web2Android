# lw.Web2Android

`lw.Web2Android` 的目标是把本地 Web 项目或在线 URL 转换为可安装的 Android APK，同时让最终用户无需安装 Android Studio、Gradle 或完整 JDK。

## 当前进度

M0 架构验证、M1 Runtime Bundle、M2 C++17 Packer CLI、M3 自动签名和 M4 完整 CLI 均已通过 CI。项目当前进入 **M5：原生 Windows GUI**。

M0 最小流水线会：

1. 用 `javac` 和 D8 将固定命名空间的 Java Runtime 编译成 `classes.dex`；
2. 用 AAPT2 动态编译 Manifest、资源和 Web assets；
3. 将预编译的 DEX 注入资源 APK；
4. 依次执行 `zipalign`、`apksigner sign` 和 `apksigner verify`；
5. 生成可安装的 `sample-debug.apk` 和 SHA-256 文件。

M0 使用一次性的测试签名，仅用于验证 APK 组装路线。正式流水线已实现“每个 Package Name 独立签名 + DPAPI 加密私钥 + 密码保护的 PKCS#12 备份”。

## 在 GitHub Actions 中验证

推送代码或手动运行唯一的 **lw.Web2Android CI** workflow。Runtime、M0、Packer、GUI 和签名集成全部成功后，只会上传一个 `lw-Web2Android-m5` Artifact：

```text
lw-Web2Android-m5/
├── bin/
│   ├── lw.Web2Android.exe
│   └── lw.Web2Android.GUI.exe
├── runtime/
│   ├── runtime-v1.zip
│   └── runtime-v1/
│       ├── classes.dex
│       └── metadata.json
├── samples/
│   ├── m0/
│   │   ├── sample-debug.apk
│   │   └── sample-debug.apk.sha256
│   └── m5/
│       ├── hello-1.0.0-android.apk
│       ├── hello-1.0.0-android.apk.sha256
│       ├── remote-1.0.0-android.apk
│       └── remote-1.0.0-android.apk.sha256
├── docs/
├── toolchain.lock.json
└── SHA256SUMS.txt
```

下载一次即可取得本轮 CI 的全部产物。M0 APK 安装后应显示 `Hello lw.Web2Android`，M5 样例则用于验证正式 Packer 的 local/remote、签名与升级身份路线。

统一 CI 会缓存 `toolchain.lock.json` 指定的 Android Platform、Build Tools、Gradle 依赖与 Gradle Build Cache。签名身份、APK、Runtime 最终产物及完整构建目录不会进入缓存。

## 本地构建

本机需要 JDK 17，以及由 `toolchain.lock.json` 锁定的 Android Platform 和 Build Tools。设置 `ANDROID_SDK_ROOT` 后运行：

```powershell
pwsh ./tools/m0-build.ps1
```

构建输出位于 `build/m0/out/`。

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

M5 开发版仍从 `ANDROID_SDK_ROOT` 和 `JAVA_HOME` 读取构建工具；本项目本机开发无需安装 Android SDK，完整 APK 由 CI 验证。面向最终用户的官方工具链首次下载、许可确认与精简 Java Runtime 管理属于 M6，当前产物不会擅自捆绑 Google Android SDK。

## Packer CLI、自动签名与身份备份

Windows C++17 Core 当前流水线负责：

1. 校验 `project.json`、本地入口或远程 URL；
2. 生成 `AndroidManifest.xml`、Android 资源和 `assets/lw-config.json`；
3. 用锁定版本 AAPT2 编译并链接资源 APK；
4. 用内置 ZIP 组装器注入 `runtime-dist/runtime-v1/classes.dex`；
5. 用 `zipalign` 生成并验证 aligned APK；
6. 为每个 Package Name 创建或复用独立的 RSA 3072 签名身份；
7. 使用 Windows DPAPI 保存加密后的 PKCS#8 私钥；
8. 使用 `apksigner` 签名、验证并生成 APK SHA-256。

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
