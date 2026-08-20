# lw.Web2Android

English | [简体中文](README.md)

Package a local static Web project—HTML, Vue, React, Vite, and similar—or a remote URL as an installable Android APK.

`lw.Web2Android` targets Windows 10/11 x64 and provides both a native GUI and a CLI. End users do not need Android Studio, Gradle, a full Android SDK, or a full JDK. After the user accepts the Android SDK License, the first-run initializer downloads a locked minimal toolchain from official sources and keeps it beside the application for reuse.

## GUI preview

<p align="center">
  <img src="assets/lw-Web2Android-GUI.jpg" alt="lw.Web2Android v0.2.6 GUI" width="680">
</p>

## Features

- Local static website and remote URL modes;
- native high-DPI Windows GUI with background builds;
- precompiled Android Runtime DEX, so Java is not rebuilt per project;
- complete AAPT2, ZIP assembly, `zipalign`, and `apksigner` pipeline;
- independent, reusable RSA-3072 signing identity per package name;
- DPAPI-encrypted private keys and password-protected PFX/P12 backups;
- machine-readable APK, certificate, Runtime, and toolchain metadata;
- rotating logs for both the Windows Packer and Android Runtime;
- standard `<input type="file">` system picker and Android `DownloadManager` support;
- HTML5 video fullscreen with Back-button exit and orientation changes;
- legacy fixed-width pages without a mobile viewport use Wide Viewport and Overview Mode for fit-to-width scaling, without layout reflow;
- CLI/project.json supports square PNG app icons and converts them to five Android Launcher mipmap densities; the bundled default icon is used when the GUI does not specify one;
- non-fatal external custom-scheme handling that preserves the current WebView;
- GitHub Actions builds the Runtime, Packer, GUI, and a real React/Vite demo.

Current version: `v0.2.6`<br>
Android: `minSdk 23`, `targetSdk 35`

## Download and first run

Download the Windows x64 ZIP from [GitHub Releases](https://github.com/lxw112190/lw.Web2Android/releases), extract the complete archive, and run:

```text
bin/lw.Web2Android.GUI.exe
```

The public distribution contains:

```text
lw-Web2Android-v0.2.6-windows-x64/
├── bin/
│   ├── lw.Web2Android.GUI.exe
│   └── lw.Web2Android.exe
├── toolchain/
│   ├── jre/
│   ├── runtime/
│   └── runtime-v6.zip
├── assets/default-app-icon.png
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
4. Optionally select a square PNG app icon; the bundled default icon is used otherwise.
5. Select orientation, fullscreen behavior, and an output directory.
6. Click **Build Android APK**.

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
  "icon": "branding/icon.png",
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

## Enterprise intranet and HTTP

The GUI provides **Allow HTTP (trusted intranet only)**. It is selected automatically when a remote URL starts with `http://`; select it manually when a packaged Vue/React application calls an HTTP API. This option generates a cleartext [Network Security Config](https://developer.android.com/privacy-and-security/security-config) and enables the [WebView mixed-content](https://developer.android.com/reference/android/webkit/WebSettings) allow mode. HTTP traffic can be observed or modified by other parties on the network, so prefer HTTPS for production deployments.

When the frontend is already hosted on an intranet server, select **Remote URL** and enter the real address, for example:

```text
http://intranet.example.test:9000/
```

`intranet.example.test` is documentation-only. Replace it with an intranet host reachable from the phone over corporate Wi-Fi, a private network, or VPN. The server must listen on a reachable interface and its firewall must allow the selected port.

For a local Vue `dist` packaged in the APK with a Spring Boot backend on the intranet:

1. set the Vue API base URL before building, for example `http://api.intranet.example.test:9001`;
2. select **Local static directory** and enable **Allow HTTP**;
3. allow the Origin `https://appassets.androidplatform.net` in Spring Boot CORS;
4. when using cookies or sessions, explicitly allow that Origin and credentials instead of using the `*` wildcard.

`WebViewAssetLoader` preserves the standard Web Origin and CORS model. lw.Web2Android does not use a Native Bridge to bypass the same-origin policy. Prefer deploying the frontend and API under one HTTPS Origin when possible.

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

Toolchain initialization log:

```text
<distribution directory>\logs\toolchain-init.log
```

Android Runtime log:

```text
/sdcard/Android/data/<Package Name>/files/logs/runtime.log
/sdcard/Android/data/<Package Name>/files/logs/device-info.log
```

For a standard `<input type="file">`, the Runtime opens the Android system picker and returns only user-selected `content://` URIs to WebView without enabling filesystem access. HTTP/HTTPS downloads run through the system `DownloadManager` and are stored under:

```text
/sdcard/Android/data/<Package Name>/files/Download/
```

Downloads may reuse the current WebView session Cookie and User-Agent, but those values are never logged. `blob:`, `data:`, and `file:` downloads are not implemented through a Native Bridge or JavaScript injection; the Runtime logs a warning and displays a lightweight message instead.

The Packer and toolchain initializer create `logs` under the current distribution directory. Initialization logging includes download URLs, SHA-256 verification, JRE selection, `sdkmanager` output, toolchain assembly, temporary-directory cleanup, and complete failure details. Every log file rotates at 2 MiB and retains up to five archives. Packer logs are UTF-8 with a BOM so Windows log viewers recognize Chinese application names correctly.

`runtime.log` uses device-local time with a UTC offset, such as `2026-08-18 17:16:49.955 +08:00`, and records Activity lifecycle, navigation, HTTP/SSL failures, WebView renderer exits, WebResourceError, JavaScript Console output, and uncaught exceptions. At each start, `device-info.log` records local time, UTC, time zone, app, device, WebView provider, network transport, Allow HTTP, Mixed Content mode, and Runtime configuration. Common password, token, Authorization, and Cookie values are redacted. The logs do not collect IMEI, Android ID, MAC address, SSID, or the phone's own IP address; start URLs are recorded without query strings or fragments. Release build timestamps remain UTC.

The Runtime also records the effective WebView viewport policy, display pixels/density, and post-load Web viewport metrics (inner/scroll dimensions and DPR), making it possible to distinguish fit-to-width scaling for fixed-width pages from responsive reflow.

## Real Web demo and CI

The single `lw.Web2Android CI` workflow:

1. builds and validates the Android Runtime;
2. assembles the locked minimal toolchain;
3. builds and tests the C++ Packer and GUI;
4. checks out a pinned revision of [wechat-article-formatter](https://github.com/lxw112190/wechat-article-formatter);
5. runs `npm ci` and `npm run build -- --base=./`;
6. packages the real React/Vite `dist` as a signed APK;
7. builds a Unicode filename fixture and verifies canonical, duplicate-free UTF-8 Web Asset entries;
8. verifies alignment, signatures, internal assets, the Runtime entry, release metadata, and signing identity reuse;
9. uploads one unified Windows x64 artifact.

The demo APK has passed validation on a physical Android device. The pinned source revision is recorded in [.github/workflows/ci.yml](.github/workflows/ci.yml), while `samples/wechat-article-formatter/SOURCE.md` in the distribution records its source, version, and build command.

Pushing a `v*` tag creates a GitHub Release only after the full workflow passes:

```bash
git tag -a v0.2.6 -m "lw.Web2Android v0.2.6"
git push origin v0.2.6
```

## Architecture

```text
Manifest + res/ ── AAPT2 ── resources.apk ─┐
                                           │
assets/lw-config.json ─────────────────────┤
assets/www/**/* (local only, UTF-8) ───────┼─ ApkAssembler ─ zipalign ─ apksigner ─ Signed APK
Precompiled classes*.dex ──────────────────┘
```

AAPT2 now handles only the Android Manifest, `res/`, `android.jar`, and the resource table; Web projects are no longer passed through `-A`. ApkAssembler reads Web files through Windows Unicode paths and injects canonical UTF-8, forward-slash ZIP entries directly, preserving Chinese, Japanese, spaces, and other Unicode filenames without asking Android resource tools to interpret them.

The Android Runtime uses `WebViewAssetLoader` and an HTTPS-style application URL for local assets. It does not use `file://` and does not expose an `addJavascriptInterface` native bridge.

## Build from source

For Packer/GUI development, install the Visual Studio 2022 **Desktop development with C++** and CMake components, open [lw.Web2Android.sln](lw.Web2Android.sln), select `Release | x64`, and build the solution. **Open a local folder** with the `Visual Studio 2022 x64` preset remains available as an alternative. See [DEVELOPMENT.md](DEVELOPMENT.md) for the complete workflow.

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-release
ctest --preset vs2022-release-tests
```

JDK 17, Gradle 8.9, and the locked Android SDK are required only when rebuilding the Android Java Runtime. End users do not need these development dependencies.

```powershell
gradle -p runtime clean :app:lintRelease :app:assembleRelease
./tools/package-runtime.ps1
```

Precompiled Runtime files and APKs are not committed; CI generates them from source. Main directories:

```text
packer/       C++17 Packer, Win32 GUI, and tests
runtime/      Android Java Runtime
samples/      Remote-mode and Unicode Assets regression configurations
tools/        Runtime, toolchain, and release scripts
.github/      Unified CI and Release workflow
```

`tools/package-vs2022-development.ps1` creates a private VS2022 development package containing source, offline dependencies, prebuilt programs, Runtime v6, and the complete minimal toolchain. Android SDK components remain subject to their license; do not upload this complete package as a public Release.

## Current limitations

- Windows 10/11 x64 hosts only;
- APK output only; AAB is not supported yet;
- custom Launcher Icons currently accept square PNG files from 192×192 through 4096×4096; Adaptive Icon layer editing is not included;
- downloads support HTTP/HTTPS only; `blob:` and `data:` downloads are not supported yet;
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
