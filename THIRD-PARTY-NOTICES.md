# Third-party notices

`lw.Web2Android` source code is licensed under the MIT License in `LICENSE`.

## Eclipse Temurin 17 JRE

Public Windows release packages include an unmodified Eclipse Temurin 17 JRE archive payload. OpenJDK is licensed under GPL v2 with the Classpath Exception and related exceptions. The original license and legal notices are preserved inside `toolchain/jre/`.

- Version: `17.0.20+8`
- Binary release: https://github.com/adoptium/temurin17-binaries/releases/tag/jdk-17.0.20%2B8
- Corresponding source project: https://github.com/adoptium/jdk17u
- License information: https://adoptium.net/what-we-do/

## Android SDK components

Public release packages do not contain Google Android SDK Platform or Build Tools. The initializer downloads the versions pinned by `toolchain.lock.json` from Android's official repository only after the local user accepts the Android SDK License:

https://developer.android.com/studio/terms

Any Android SDK files in a locally generated `*-complete-private.zip` remain subject to that license and are not covered by this project's MIT License.

## AndroidX WebKit

The precompiled Android Runtime uses AndroidX WebKit, distributed under the Apache License 2.0:

https://github.com/androidx/androidx/blob/androidx-main/LICENSE.txt

## spdlog

The Windows Packer and GUI statically link spdlog 1.17.0, including its bundled fmt implementation, under the MIT License. The complete notice is included in `third_party/licenses/spdlog-LICENSE.txt`.

- Source: https://github.com/gabime/spdlog/tree/v1.17.0
- License: https://github.com/gabime/spdlog/blob/v1.17.0/LICENSE

## wechat-article-formatter demo

CI builds a real Web demo from a pinned revision of `wechat-article-formatter` and includes the generated APK in the unified distribution. The source repository is MIT licensed. Its license is copied beside the Demo APK as `DEMO-LICENSE.txt`.

- Source: https://github.com/lxw112190/wechat-article-formatter
