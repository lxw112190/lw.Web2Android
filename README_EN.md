# lw.Web2Android

English | [简体中文](README.md)

Package a local static Web project—HTML, Vue, React, Vite, and similar—or a remote URL as an installable Android APK.

`lw.Web2Android` targets Windows 10/11 x64 and provides both a native GUI and a CLI. End users do not need Android Studio, Gradle, a full Android SDK, or a full JDK. After the user accepts the Android SDK License, the first-run initializer downloads a locked minimal toolchain from official sources and keeps it beside the application for reuse.

## Features

- Local static website and remote URL modes;
- native high-DPI Windows GUI with background builds;
- precompiled Android Runtime DEX, so Java is not rebuilt per project;
- complete AAPT2, ZIP assembly, `zipalign`, and `apksigner` pipeline;
- independent, reusable RSA-3072 signing identity per package name;
- DPAPI-encrypted private keys and password-protected PFX/P12 backups;
- machine-readable APK, certificate, Runtime, and toolchain metadata;
- rotating logs for both the Windows Packer and Android Runtime;
- GitHub Actions builds the Runtime, Packer, GUI, and a real React/Vite demo.

Current version: `v0.2.0`<br>
Android: `minSdk 23`, `targetSdk 35`

## Download and first run

Download the Windows x64 ZIP from [GitHub Releases](https://github.com/lxw112190/lw.Web2Android/releases), extract the complete archive, and run:

```text
bin/lw.Web2Android.GUI.exe
```

The public distribution contains:

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

The public package does not redistribute the Android SDK or Google Build Tools. Click **Initialize Toolchain** in the GUI, review and accept the [Android SDK License](https://developer.android.com/studio/terms), and the application will download and verify the locked command-line tools, Android Platform, and Build Tools into its own directory:

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

PowerShell can initialize it as well:

```powershell
./tools/install-minimal-toolchain.ps1 -AcceptAndroidSdkLicense
```

Downloads are checked with SHA-256 and temporary files are removed. Once initialized, the application does not depend on system `ANDROID_SDK_ROOT` or `JAVA_HOME` settings.

## Build an APK with the GUI

1. Choose **Local website** or **Remote URL**.
2. For local mode, select the build output that contains `index.html`, such as a Vite `dist` directory.
3. Enter the app name, package name, version name, and version code.
4. Select orientation, fullscreen behavior, and an output directory.
5. Click **Build Android APK**.

Local Web assets must use relative URLs. A Vite project should normally set:

```ts
import { defineConfig } from "vite";

export default defineConfig({
  base: "./",
});
```

The build can also override the base path:

```bash
npm run build -- --base=./
```

Without this setting, `/assets/...` points to the Android application origin instead of `assets/www/` inside the APK.

## CLI

Example `project.json`:

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

Build and manage signing identities:

```powershell
./bin/lw.Web2Android.exe validate project.json
./bin/lw.Web2Android.exe build project.json
./bin/lw.Web2Android.exe signing info com.example.mywebapp
./bin/lw.Web2Android.exe signing export com.example.mywebapp D:\backup\mywebapp.pfx
```

Paths are resolved relative to `project.json`. `entry` is relative to `source` and is converted to `assets/www/<entry>` in the APK. Remote mode uses `mode: "remote"` and `url`; plain HTTP is accepted only with `allowHttp: true`.

See [packer/README.md](packer/README.md) for all CLI options.

## Output, signing, and logs

Every successful build produces:

```text
<App>-<Version>-android.apk
<APK>.sha256
<APK name>.release.json
<APK name>-RELEASE.md
```

The same package name reuses the same certificate, allowing future APKs to upgrade an installed application. Signing identities are stored by default under:

```text
%LOCALAPPDATA%\lw.Web2Android\keys\<Package Name>\
```

Export and keep an offline PFX/P12 backup. Losing the signing identity means future builds cannot upgrade the existing Android application.

Windows Packer log:

```text
<distribution directory>\logs\packer.log
```

Android Runtime log:

```text
/sdcard/Android/data/<Package Name>/files/logs/runtime.log
```

The Packer creates `logs` under the current distribution directory. When an executable is used standalone, it creates `logs` beside that executable. Both logs rotate at 2 MiB per file and retain up to five archives. Runtime logging includes navigation, HTTP/SSL failures, WebView renderer exits, JavaScript Console output, and uncaught exceptions.

## Real Web demo and CI

The single `lw.Web2Android CI` workflow:

1. builds and validates the Android Runtime;
2. assembles the locked minimal toolchain;
3. builds and tests the C++ Packer and GUI;
4. checks out a pinned revision of [wechat-article-formatter](https://github.com/lxw112190/wechat-article-formatter);
5. runs `npm ci` and `npm run build -- --base=./`;
6. packages the real React/Vite `dist` as a signed APK;
7. verifies alignment, signatures, internal assets, the Runtime entry, release metadata, and signing identity reuse;
8. uploads one unified Windows x64 artifact.

The demo APK has passed validation on a physical Android device. The pinned source revision is recorded in [.github/workflows/ci.yml](.github/workflows/ci.yml), while `samples/wechat-article-formatter/SOURCE.md` in the distribution records its source, version, and build command.

Pushing a `v*` tag creates a GitHub Release only after the full workflow passes:

```bash
git tag -a v0.2.0 -m "lw.Web2Android v0.2.0"
git push origin v0.2.0
```

## Architecture

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

The Android Runtime uses `WebViewAssetLoader` and an HTTPS-style application URL for local assets. It does not use `file://` and does not expose an `addJavascriptInterface` native bridge.

## Build from source

Development requires CMake, the Visual Studio 2022 C++ toolchain, JDK 17, Gradle 8.9, and the locked Android SDK. End users do not need these development dependencies.

```powershell
gradle -p runtime clean :app:lintRelease :app:assembleRelease
./tools/package-runtime.ps1

cmake -S packer -B build/packer -A x64 -DBUILD_TESTING=ON
cmake --build build/packer --config Release --parallel
ctest --test-dir build/packer -C Release --output-on-failure
```

Precompiled Runtime files and APKs are not committed; CI generates them from source. Main directories:

```text
packer/       C++17 Packer, Win32 GUI, and tests
runtime/      Android Java Runtime
samples/      Remote-mode test configuration
tools/        Runtime, toolchain, and release scripts
.github/      Unified CI and Release workflow
```

## Current limitations

- Windows 10/11 x64 hosts only;
- APK output only; AAB is not supported yet;
- custom application icons are not exposed in the GUI yet;
- Web file selection and download management are not implemented yet;
- no native bridge.

## License

[MIT License](LICENSE). Author: 天天代码码天天.

See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for third-party notices. The Android SDK and Google Build Tools are governed by their own licenses and are not covered by this project's MIT License.

## Contact and support

- Author: 天天代码码天天
- QQ: 819069052
- QQ Group: C# 人工智能实践 (758616458)

If this project helps you, you can support its maintenance by scanning the QR code:

<p align="center">
  <img src="assets/sponsor.jpg" alt="WeChat sponsorship" width="320">
</p>
