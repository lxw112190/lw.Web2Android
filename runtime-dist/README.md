# Runtime Distribution

`runtime-v1/` 是 Runtime CI 生成并经 `tools/package-runtime.ps1` 校验的可复用 Bundle。Packer 只注入这里的 DEX，不会在打包每个应用时重新运行 Gradle。
