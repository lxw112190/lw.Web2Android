# Android Runtime

本目录是 `lw.Web2Android` 的固定 Java Runtime。它由 CI 编译、R8 精简并提取为可重复注入最终 APK 的 `classes.dex`。

Release 构建同时启用 R8 代码收缩与优化资源收缩。固定动态清单引用的 `MainActivity` 和 `RuntimeFileProvider` 入口，其他 Runtime 实现由代码可达性保留，自动生成的 `R`/`BuildConfig` 类则由 R8 移除。AndroidX Core 携带但 Runtime 未使用的通知资源会在生成 Bundle 前移除；打包脚本只允许由 Packer 同步生成的相机路径 XML 模板，拒绝其他真实 `res/` 文件，并拒绝 DEX 中残留的应用 `R`/`BuildConfig` 描述符，确保 Runtime 不暗中依赖某个固定 APK 的资源表。

## 当前能力

- `WebViewAssetLoader` 本地 HTTPS 风格 URL；
- `assets/lw-config.json` schema 2，本地入口使用与 APK 一致的 `www/<entry>` 资产路径；
- local 与 remote 两种模式；
- JavaScript、DOM Storage、localStorage 与 IndexedDB；
- WebView 历史返回；
- Android 13+ 使用系统 `OnBackInvokedCallback`，API 23–32 保留 `onBackPressed()`；全屏/历史状态变化时动态注册回调，首页交给系统默认返回行为，不强制双击退出；
- fullscreen 与 auto/portrait/landscape；
- HTTP 默认阻止，只有 `allowHttp=true` 才允许；
- 非 HTTP(S) URL 交给系统应用处理；
- 基本主页面错误提示。
- 2 MiB × 5 的 Runtime 与设备信息独立轮转日志，并同步输出 Logcat；
- 页面加载、HTTP/SSL、WebView renderer、JS Console 与未捕获异常诊断。
- 每次启动记录应用、设备、WebView、网络传输类型和 Runtime 配置摘要。
- 标准 `<input type="file">` 调用 Android 系统文件选择器，仅接受 `content://` 结果；图片单选同时支持 `capture`，通过私有缓存和 FileProvider 直接调起系统相机并返回全尺寸照片；
- 普通选图与相机返回 URI 的回退分支同时读取 `Intent.data` 和 `ClipData`，支持多选模式下仅选择一张图片；按首次出现顺序去重，最多交付 20 个 `content://` URI，不扩大文件访问权限；
- HTTP/HTTPS 下载交给 Android `DownloadManager`，保存在应用专属 `files/Download`；
- HTML5 视频通过 `WebChromeClient` 进入全屏，支持返回键退出和方向恢复；
- 对没有移动端 viewport 的固定宽度网页启用 Wide Viewport 与 Overview Mode，按设备宽度整体缩放而不重新排版；
- Custom Scheme 无可用处理程序时只记录 WARN 和 Toast，不销毁当前 WebView。
- local 模式可接收单条分享文本或用户明确打开的 `content://` 文本/配置文件，并通过 `lw:external-content` CustomEvent 单向交给本地页面；
- 外部内容严格执行类型、8 MiB 默认上限、二进制与 UTF-8/UTF-16 BOM 编码检查；不支持多文件或写回原 URI。

Runtime 启动日志会记录最终生效的 `WebView viewport: wide=true, overview=true`、屏幕像素/密度和 WebView Provider；每次页面完成后以 DEBUG 记录 `innerWidth`、`innerHeight`、`scrollWidth`、`scrollHeight`、`screenWidth`、`screenHeight` 与 `devicePixelRatio`，用于区分 fit-to-width 缩放和真正的响应式重排。

选图结果记录 `File chooser result; status=...`，区分 `accepted`、`canceled`、`non-success`、`empty` 和 `filtered`；仅记录 resultCode、data URI 是否存在、ClipData 数量、接受/过滤/去重数量和是否达到上限，不记录完整 URI、文件名、路径或图片内容。解析异常、回调缺失和交付失败单独记录。取消与非成功结果不交付文件；不再依赖可能忽略 ClipData 的框架 `FileChooserParams.parseResult()`。

HTML5 全屏方向策略为：Landscape 应用使用 `sensorLandscape`，Portrait/Auto 应用使用 `sensor`，退出全屏后恢复进入前的 requested orientation；方向变化会写入 DEBUG 日志。

Runtime 不调用 `addJavascriptInterface`，不提供 Native Bridge，也不引用应用动态生成的 `R` 类。外部内容只会投递到 `https://appassets.androidplatform.net/assets/` 可信本地页面，remote 模式完全禁用。

## 真机日志

返回处理统一为“退出 HTML5 全屏 → WebView 历史返回 → 系统默认行为”，不再通过 `onKeyDown(KEYCODE_BACK)` 重复拦截。生成的 Manifest 与 Runtime 构建 Manifest 均启用 `enableOnBackInvokedCallback`。历史更新（包括 History API/Hash 路由）、页面完成、状态恢复及视频全屏切换时同步回调状态；销毁时注销。

Packer 现在要求 Runtime metadata 声明新的 `capabilities.backNavigation` 能力，防止新 Manifest 与仅支持旧返回回调的 DEX 混用。升级必须使用本次 CI 新生成的 `toolchain/runtime/` 和 `runtime-v6.zip`；复制旧工具链时只能复用其他锁定组件。旧 Bundle 会在打包前被明确拒绝，而不是生成可能直接退出的 APK。

进入 Runtime 的返回事件记录 `Back navigation; source=legacy/platform-invoked, action=EXIT_FULLSCREEN/WEB_HISTORY/SYSTEM`，以及 fullscreen、canGoBack、historyIndex、historySize，不记录路由参数或页面内容。现代系统在首页未注册应用回调，由系统直接处理，不保证每次系统返回都有应用日志。

`samples/back-navigation` 提供 A→B→C 和 SPA pushState/Hash 测试。CI 验证样例 APK 结构，主机测试验证返回策略；系统手势、三键返回、键盘收起及全屏退出仍需 Android 12/13+ 真机验收。仅切换组件、不入栈或使用 replace 的网页不会自动产生上一页；不注入 Native Bridge 或伪造前端路由历史。

Runtime 优先写入无需额外存储权限的应用专属外部目录：

```text
/sdcard/Android/data/<Package Name>/files/logs/runtime.log
/sdcard/Android/data/<Package Name>/files/logs/device-info.log
```

`runtime.log` 与 `device-info.log` 分别达到 2 MiB 后依次轮转为 `.1` 至 `.5`。Runtime 日志使用手机本地时间与 UTC 偏移，并包含级别、PID 与线程；设备日志同时保存本地时间、UTC 与时区。URL 查询参数及 fragment 不会写入，常见 Password、Token、Authorization、Cookie 值会被脱敏。设备日志不采集 IMEI、Android ID、MAC、SSID 或手机本机 IP。通过 USB 调试复制：

```bash
adb pull /sdcard/Android/data/<Package Name>/files/logs ./android-runtime-logs
adb logcat -s lw.Web2Android
```

外部应用目录不可用时回退到内部 `files/logs`。Runtime 错误页会显示实际日志路径。

## 构建

CI 使用 JDK 17、Gradle 8.9、AGP 8.7.3、API 35、AndroidX Core 1.15.0 和 AndroidX WebKit 1.15.0。选择 WebKit 1.15.0 是为了继续支持项目当前的 minSdk 23；WebKit 1.16 及以上已经将 minSdk 提升到 24。

```text
gradle -p runtime :app:testReleaseUnitTest :app:lintRelease :app:assembleRelease
pwsh ./tools/package-runtime.ps1
```

最终输出：

```text
build/runtime-dist/runtime-v6.zip
```

`FileChooserResultTest` 使用仅测试依赖 Robolectric 4.16.1（API 28 沙箱）验证真实 Intent/ClipData 对象的单选、多选、双通道去重、取消、空返回、不安全 URI、数量上限及日志隐私。Robolectric 不进入 APK；完整 R8/资源检查和 Runtime Bundle 生成仍在 CI 执行。
