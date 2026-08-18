# Packer CLI（M4）

Packer 是 Windows C++17 程序。它读取 `project.json`，动态生成 Manifest、资源和 Runtime 配置，通过锁定版本的 AAPT2 生成资源 APK，在内部注入预编译 Runtime DEX，执行 `zipalign` 后使用 Package 独立身份签名并验证 APK。

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
build/packer/Release/lw.Web2Android.exe signing info com.lw.samples.hello
build/packer/Release/lw.Web2Android.exe signing export com.lw.samples.hello D:\backup\com.lw.samples.hello.pfx
```

可用参数：

```text
--android-sdk <directory>  覆盖 ANDROID_SDK_ROOT
--java-home <directory>    覆盖 JAVA_HOME
--runtime <directory>      覆盖 project.json 中的 Runtime Bundle 目录
--keys-dir <directory>     覆盖 DPAPI 签名身份存储目录
--keep-work-dir            保留中间目录用于诊断
```

默认签名身份位于 `%LOCALAPPDATA%\lw.Web2Android\keys\<Package Name>\`：

```text
signing.key.lw   DPAPI 加密的 PKCS#8 私钥
certificate.pem  X.509 自签名证书
metadata.json    Package、证书 SHA-256 与创建时间
```

私钥仅在调用 `apksigner` 时解密到隔离工作目录，签名结束后立即覆盖删除。相同 Package 会复用同一证书，不同 Package 不共享私钥。每次构建同时输出 `<APK>.sha256`。

`signing info` 只读取已有身份，不会自动创建新密钥。`signing export` 会在控制台关闭回显后要求输入并确认至少 8 个字符的密码，生成包含证书和私钥的标准 PKCS#12 备份；密码不会进入命令行或日志。目标文件已存在时命令会拒绝覆盖。请把备份和密码分开保管，丢失签名身份后将无法发布可覆盖安装的更新。

GitHub Actions 使用统一的 `lw.Web2Android CI` workflow。Packer EXE、Runtime Bundle、M0 APK、M4 local/remote 签名 APK 和总校验文件会合并到单个 `lw-Web2Android-m4` Artifact 中。

## project.json

本地模式参考 `samples/hello/project.json`，在线模式参考 `samples/remote/project.json`。路径均相对于配置文件解析，HTTP 仅在 `allowHttp` 为 `true` 时允许。
