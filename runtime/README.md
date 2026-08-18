# Android Runtime

本目录是 `lw.Web2Android` 的固定 Java Runtime。它由 CI 编译、R8 精简并提取为可重复注入最终 APK 的 `classes.dex`。

Release 构建同时启用 R8 代码收缩与优化资源收缩。AndroidX Core 携带但 Runtime 未使用的通知资源会在生成 Bundle 前移除；打包脚本仍会拒绝任何剩余的真实 `res/` 文件，确保当前 Runtime 不暗中依赖动态 APK 的资源表。

## 当前能力（M1）

- `WebViewAssetLoader` 本地 HTTPS 风格 URL；
- `assets/lw-config.json` schema 1；
- local 与 remote 两种模式；
- JavaScript、DOM Storage、localStorage 与 IndexedDB；
- WebView 历史返回；
- fullscreen 与 auto/portrait/landscape；
- HTTP 默认阻止，只有 `allowHttp=true` 才允许；
- 非 HTTP(S) URL 交给系统应用处理；
- 基本主页面错误提示。

Runtime 不调用 `addJavascriptInterface`，不提供 Native Bridge，也不引用应用动态生成的 `R` 类。

## 构建

CI 使用 JDK 17、Gradle 8.9、AGP 8.7.3、API 35 和 AndroidX WebKit 1.15.0。选择 WebKit 1.15.0 是为了继续支持项目当前的 minSdk 23；WebKit 1.16 及以上已经将 minSdk 提升到 24。

```text
gradle -p runtime :app:lintRelease :app:assembleRelease
pwsh ./tools/package-runtime.ps1
```

最终输出：

```text
build/runtime-dist/runtime-v1.zip
```
