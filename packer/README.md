# Packer CLI（M2）

Packer 是 Windows C++17 程序。它读取 `project.json`，动态生成 Manifest、资源和 Runtime 配置，通过锁定版本的 AAPT2 生成资源 APK，在内部注入预编译 Runtime DEX，最后用 `zipalign` 输出 unsigned APK。

## 构建

```powershell
cmake -S packer -B build/packer -A x64 -DBUILD_TESTING=ON
cmake --build build/packer --config Release
ctest --test-dir build/packer -C Release --output-on-failure
```

## 使用

```powershell
build/packer/Release/lw.Web2Android.exe validate samples/hello/project.json
build/packer/Release/lw.Web2Android.exe build samples/hello/project.json
```

可用参数：

```text
--android-sdk <directory>  覆盖 ANDROID_SDK_ROOT
--runtime <directory>      覆盖 project.json 中的 Runtime Bundle 目录
--keep-work-dir            保留中间目录用于诊断
```

M2 不执行签名，因此输出文件不能直接安装。M3 将在相同 `BuildPipeline` 后加入签名身份管理、`apksigner` 和签名验证。

## project.json

本地模式参考 `samples/hello/project.json`，在线模式参考 `samples/remote/project.json`。路径均相对于配置文件解析，HTTP 仅在 `allowHttp` 为 `true` 时允许。
