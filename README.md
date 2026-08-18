# lw.Web2Android

[English](README_EN.md) | 简体中文

将本地 HTML、Vue、React、Vite 等静态 Web 项目，或一个在线 URL，打包为可安装的 Android APK。

`lw.Web2Android` 面向 Windows 10/11 x64，提供原生 GUI 和 CLI。最终用户不需要安装 Android Studio、Gradle、完整 Android SDK 或完整 JDK；首次使用时确认 Android SDK License，程序会从官方源下载锁定版本的最小组件，并保存在应用程序目录中供后续复用。

## 主要能力

- 本地静态网站和在线 URL 两种模式；
- 原生 Windows GUI，支持高 DPI 和后台构建；
- 预编译 Android Runtime DEX，无需为每个项目重新编译 Java；
- AAPT2 资源生成、ZIP 组装、`zipalign` 和 `apksigner` 完整流水线；
- 每个 Package Name 独立且可复用的 RSA 3072 签名身份；
- Windows DPAPI 加密私钥，以及密码保护的 PFX/P12 备份；
- APK、证书和工具链版本的机器可读发行元数据；
- Packer 与 Android Runtime 轮转日志；
- GitHub Actions 自动构建 Runtime、Packer、GUI 和真实 React/Vite Demo。

当前版本：`v0.2.0`<br>
Android：`minSdk 23`，`targetSdk 35`

## 下载与首次使用

从 [GitHub Releases](https://github.com/lxw112190/lw.Web2Android/releases) 下载 Windows x64 ZIP 并完整解压，然后运行：

```text
bin/lw.Web2Android.GUI.exe
```

公开发行包包含：

```text
lw-Web2Android-v0.2.0-windows-x64/
├── bin/
│   ├── lw.Web2Android.GUI.exe
│   └── lw.Web2Android.exe
├── toolchain/
│   ├── jre/
│   ├── runtime/
│   └── runtime-v1.zip
├── tools/
├── samples/wechat-article-formatter/
├── docs/
├── SHA256SUMS.txt
├── LICENSE
└── THIRD-PARTY-NOTICES.md
```

公开包不会重新分发 Android SDK 或 Google Build Tools。首次点击 GUI 中的“初始化工具链”，阅读并接受 [Android SDK License](https://developer.android.com/studio/terms) 后，程序会下载并校验锁定版本的命令行工具、Android Platform 和 Build Tools，最终放在当前应用目录：

```text
toolchain/
├── aapt2.exe
├── zipalign.exe
├── android.jar
├── apksigner/
├── jre/
└── runtime/
    └── classes.dex
```

也可以在 PowerShell 中初始化：

```powershell
./tools/install-minimal-toolchain.ps1 -AcceptAndroidSdkLicense
```

下载文件会进行 SHA-256 校验，临时文件会自动清理。初始化完成后不依赖系统的 `ANDROID_SDK_ROOT` 或 `JAVA_HOME`。

## 使用 GUI 生成 APK

1. 选择“本地网页”或“在线网址”。
2. 本地模式选择包含 `index.html` 的构建输出目录，例如 Vite 的 `dist`。
3. 填写应用名称、Package Name、Version Name 和 Version Code。
4. 选择屏幕方向、全屏选项和输出目录。
5. 点击“生成 Android APK”。

本地 Web 资源必须使用相对 URL。Vite 项目推荐配置：

```ts
import { defineConfig } from "vite";

export default defineConfig({
  base: "./",
});
```

也可以在 CI 或命令行构建时覆盖：

```bash
npm run build -- --base=./
```

否则 `/assets/...` 会指向 Android 应用域名根目录，而不是 APK 内的 `assets/www/`。

## CLI

示例 `project.json`：

```json
{
  "schemaVersion": 1,
  "mode": "local",
  "name": "My Web App",
  "packageName": "com.example.mywebapp",
  "versionName": "1.0.0",
  "versionCode": 1,
  "source": "dist",
  "entry": "index.html",
  "fullscreen": false,
  "orientation": "auto",
  "allowHttp": false,
  "output": "output"
}
```

构建和签名身份管理：

```powershell
./bin/lw.Web2Android.exe validate project.json
./bin/lw.Web2Android.exe build project.json
./bin/lw.Web2Android.exe signing info com.example.mywebapp
./bin/lw.Web2Android.exe signing export com.example.mywebapp D:\backup\mywebapp.pfx
```

路径相对于 `project.json` 解析。`entry` 相对于 `source`；打包后会自动转换为 APK 内的 `assets/www/<entry>`。远程模式使用 `mode: "remote"` 和 `url`，HTTP 只有在 `allowHttp: true` 时允许。

完整 CLI 参数见 [packer/README.md](packer/README.md)。

## 输出、签名与日志

每次成功构建会生成：

```text
<应用名>-<版本>-android.apk
<APK>.sha256
<APK名称>.release.json
<APK名称>-RELEASE.md
```

同一个 Package Name 会复用同一个证书，使新 APK 能覆盖安装旧版本。默认签名身份保存在：

```text
%LOCALAPPDATA%\lw.Web2Android\keys\<Package Name>\
```

请导出并离线保存 PFX/P12 备份。签名身份丢失后，无法再发布可覆盖安装的更新。

Packer 日志：

```text
%LOCALAPPDATA%\lw.Web2Android\logs\packer.log
```

Android Runtime 日志：

```text
/sdcard/Android/data/<Package Name>/files/logs/runtime.log
```

两类日志均按单文件 2 MiB 轮转，最多保留 5 个归档。Runtime 日志同时记录页面加载、HTTP/SSL、WebView renderer、JavaScript Console 和未捕获异常。

## 真实 Web Demo 与 CI

统一的 `lw.Web2Android CI` 会：

1. 编译并检查 Android Runtime；
2. 组装锁定版本的最小工具链；
3. 编译和测试 C++ Packer/GUI；
4. 检出 [wechat-article-formatter](https://github.com/lxw112190/wechat-article-formatter) 的固定提交；
5. 执行 `npm ci` 和 `npm run build -- --base=./`；
6. 将真实 React/Vite `dist` 打包为签名 APK；
7. 验证 APK 对齐、签名、内部资源、Runtime 入口、发行元数据和签名身份复用；
8. 上传一个统一的 Windows x64 Artifact。

该 Demo 已在 Android 真机验证通过。固定源码提交记录在 [.github/workflows/ci.yml](.github/workflows/ci.yml)，发行包中的 `samples/wechat-article-formatter/SOURCE.md` 记录来源、版本和构建命令。

推送 `v*` 标签时，完整 CI 成功后会自动创建 GitHub Release：

```bash
git tag -a v0.2.0 -m "lw.Web2Android v0.2.0"
git push origin v0.2.0
```

## 架构

```text
Web dist / Remote URL
        │
        ├── Manifest + Resources ── AAPT2
        ├── assets/lw-config.json
        └── assets/www/* (local only)
                         │
Precompiled Runtime DEX ─┤
                         ▼
                  Resource APK
                         │
                 ZIP assembly
                         │
                    zipalign
                         │
                    apksigner
                         ▼
                    Signed APK
```

Android Runtime 使用 `WebViewAssetLoader` 通过 HTTPS 风格的应用内 URL 加载本地资源，不使用 `file://`，也不提供 `addJavascriptInterface` Native Bridge。

## 从源码构建

开发环境需要 CMake、Visual Studio 2022 C++ 工具链、JDK 17、Gradle 8.9 和锁定版本的 Android SDK。普通用户不需要这些开发依赖。

```powershell
gradle -p runtime clean :app:lintRelease :app:assembleRelease
./tools/package-runtime.ps1

cmake -S packer -B build/packer -A x64 -DBUILD_TESTING=ON
cmake --build build/packer --config Release --parallel
ctest --test-dir build/packer -C Release --output-on-failure
```

仓库不提交预编译 Runtime 或 APK；这些文件由 CI 从源码生成。主要目录：

```text
packer/       C++17 Packer、Win32 GUI 与测试
runtime/      Android Java Runtime
samples/      Remote 模式测试配置
tools/        Runtime、工具链与发行打包脚本
.github/      统一 CI 与 Release 流程
```

## 当前限制

- 仅支持 Windows 10/11 x64 主机；
- 当前输出 APK，不输出 AAB；
- GUI 暂不支持自定义应用图标；
- Runtime 暂未实现网页文件选择和下载管理；
- 不提供 Native Bridge。

## License

[MIT License](LICENSE)，作者：天天代码码天天。

第三方组件及许可见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。Android SDK 与 Google Build Tools 受其各自许可约束，不属于本项目 MIT License 的授权范围。

## 联系与支持

- 作者：天天代码码天天
- QQ：819069052
- QQ Group：C# 人工智能实践（群号：758616458）

如果项目对你有帮助，可以扫码支持维护：

![微信赞助](assets/sponsor.jpg)
