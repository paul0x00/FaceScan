# 3rdParty 目录说明

此目录存放**无法通过 vcpkg 分发的厂商闭源 SDK**（目前只有奥比中光 Orbbec SDK）。
当前仓库已提交 Windows 与 macOS 平台的 Orbbec SDK，克隆后可直接用于对应平台构建；Linux 目录仍为预留，需要时按相同结构补充。

通用依赖（Boost、SQLite3、ZLIB、GoogleTest）均由 vcpkg 管理，见 `backend/vcpkg.json`，与本目录无关。

## 目录布局

CMake 按当前平台自动选择子目录（见 `backend/CMakeLists.txt` 中 `FACESCAN_ORBBEC_SDK_DIR`）：

```
3rdParty/orbbec/
├── macos/                  # macOS 平台 SDK
│   ├── include/libobsensor/    # ObSensor.hpp 及 h/、hpp/ 头文件
│   └── lib/                    # libOrbbecSDK*.dylib、extensions/、OrbbecSDKConfig.xml
├── windows/                # Windows 平台 SDK
│   ├── include/libobsensor/
│   ├── lib/OrbbecSDK.lib       # 链接用导入库
│   └── bin/OrbbecSDK.dll       # 运行时动态库，构建后自动复制到可执行文件目录
└── linux/                  # 预留，结构同 macos（lib 下放 libOrbbecSDK*.so）
```

## 更新 SDK

- 奥比中光开发者社区 / GitHub Releases（OrbbecSDK v2）：<https://github.com/orbbec/OrbbecSDK_v2/releases>
- 下载对应平台压缩包后，把 `include/` 与 `lib/`（Windows 还有 `bin/` 下的 dll）拷入上述对应目录。
- 替换 SDK 后需要重新运行 CMake 配置，确认输出中出现 `Orbbec SDK found: ...`。
- Windows 构建后会把 `bin/` 下的 dll/xml 自动复制到 `facescan_backend.exe` 和 `facescan_tests.exe` 所在目录。

## 行为说明

- SDK **缺失时不影响构建**：CMake 检测不到时跳过 `FACESCAN_WITH_ORBBEC`，后端以 `mock` 相机模式运行完整流程。
- 想把 SDK 放在其他位置时，配置时传 `-DFACESCAN_ORBBEC_SDK_DIR=/path/to/sdk` 覆盖。
