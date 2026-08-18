# lw.Web2Android

`lw.Web2Android` 的目标是把本地 Web 项目或在线 URL 转换为可安装的 Android APK，同时让最终用户无需安装 Android Studio、Gradle 或完整 JDK。

## 当前进度

M0 架构验证已经由 GitHub Actions 成功产出并验收可安装 APK，M1 Runtime Bundle 也已通过 CI。项目当前进入 **M2：C++17 Packer CLI**。

M0 最小流水线会：

1. 用 `javac` 和 D8 将固定命名空间的 Java Runtime 编译成 `classes.dex`；
2. 用 AAPT2 动态编译 Manifest、资源和 Web assets；
3. 将预编译的 DEX 注入资源 APK；
4. 依次执行 `zipalign`、`apksigner sign` 和 `apksigner verify`；
5. 生成可安装的 `sample-debug.apk` 和 SHA-256 文件。

M0 使用一次性的测试签名，仅用于验证 APK 组装路线。正式的“每个 Package Name 独立签名 + DPAPI”将在 M3 实现。

## 在 GitHub Actions 中验证

推送代码或手动运行 **M0 APK Proof of Concept** workflow。成功后下载 `m0-sample-debug-apk` artifact，其中包含：

```text
sample-debug.apk
sample-debug.apk.sha256
```

安装 APK 后应显示 `Hello lw.Web2Android`，并且页面中的 JavaScript 计数按钮可正常工作。

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

M1 Runtime 已实现首版 `WebViewAssetLoader`、配置读取及 local/remote 加载，并建立独立 CI。GUI 仍需等待 CLI Core 稳定后再开发。

## M2 Packer CLI

M2 已建立独立于 GUI 的 Windows C++17 Core，当前流水线负责：

1. 校验 `project.json`、本地入口或远程 URL；
2. 生成 `AndroidManifest.xml`、Android 资源和 `assets/lw-config.json`；
3. 用锁定版本 AAPT2 编译并链接资源 APK；
4. 用内置 ZIP 组装器注入 `runtime-dist/runtime-v1/classes.dex`；
5. 用 `zipalign` 生成并验证 unsigned APK。

构建和命令行用法参见 `packer/README.md`。`packer-ci` 负责编译与单元测试，`integration-ci` 负责生成 local/remote 两个 unsigned APK。签名将在 M3 加入。
