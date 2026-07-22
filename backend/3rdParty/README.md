# 3rdParty 目录说明

此目录存放**无法通过 vcpkg 分发的厂商闭源 SDK**：奥比中光 Orbbec、海康威视 MVS 和迈德威视 SDK。
当前仓库已提交 Windows 与 macOS 平台的 Orbbec SDK，克隆后可直接用于对应平台构建；Linux Orbbec SDK、海康 MVS SDK和迈德威视 SDK需要按下述布局手工补充。

通用依赖（Boost、SQLite3、ZLIB、GoogleTest）由 vcpkg 管理，见 `backend/vcpkg.json`。

## 目录布局

```text
3rdParty/
├── orbbec/
│   ├── macos/
│   │   ├── include/libobsensor/
│   │   └── lib/libOrbbecSDK*.dylib
│   ├── windows/
│   │   ├── include/libobsensor/
│   │   ├── lib/OrbbecSDK.lib
│   │   └── bin/OrbbecSDK.dll
│   └── linux/
│       ├── include/libobsensor/
│       └── lib/libOrbbecSDK*.so
├── hikvision/windows/
│   ├── include/MvCameraControl.h
│   ├── lib/MvCameraControl.lib
│   └── bin/MvCameraControl.dll
└── mindvision/windows/
    ├── include/CameraApi.h
    ├── lib/MVCAMSDK_X64.lib
    └── bin/MVCAMSDK_X64.dll
```

## 更新 SDK

- 奥比中光开发者社区 / GitHub Releases（OrbbecSDK v2）：<https://github.com/orbbec/OrbbecSDK_v2/releases>。下载对应平台压缩包后，把 `include/` 与 `lib/`（Windows 还有 `bin/` 下的 DLL）拷入上述对应目录。
- 海康威视：安装或下载 MVS 客户端/SDK 后复制上述头文件、导入库和运行库。
- 迈德威视：安装或下载工业相机 SDK 后复制上述头文件、导入库和运行库。
- 替换 SDK 后需要重新运行 CMake 配置，确认输出中出现对应的 SDK found 信息。
- Windows 构建后会把检测到的厂商 DLL/XML 自动复制到 `FaceScanCamera.dll` 所在目录。

请遵守各厂商 SDK 的许可证，不要把 SDK 二进制提交到仓库。

## CMake 探测与覆盖路径

CMake 会自动探测上述默认目录；也可通过以下变量指定其他位置：

```text
-DFACESCAN_ORBBEC_SDK_DIR=/path/to/orbbec
-DFACESCAN_HIKVISION_SDK_DIR=/path/to/hikvision
-DFACESCAN_MINDVISION_SDK_DIR=/path/to/mindvision
```

SDK 缺失不会影响 `mock` 模式构建。`orbbec`/`gemini215` 仅要求 Orbbec SDK；`multi_camera` 要求 Windows 构建同时检测到 Orbbec、海康和迈德威视 SDK。Windows 构建完成后，检测到的厂商 DLL 会自动复制到后端可执行文件目录。

## `multi_camera` 运行约定

- 设备角色：3 台 Orbbec（左、右、下）、1 台海康彩色相机（正面）、1 台迈德威视双目相机。
- 绑定顺序：优先使用配置中的序列号，未配置或未命中时按各 SDK 枚举顺序兜底。
- 默认触发：三台 Orbbec 并行完成彩色、左 IR、右 IR 采集，再同步触发海康与迈德威视模组。
- 参数联动：拍摄页曝光和增益下发到全部设备；Orbbec 的彩色与 IR 使用同一组参数，曝光限制为 `1–100 ms`，增益限制为 `1–20 倍`。
- 文件格式：所有厂商原图均写为 BMP，并按 `front`、`left`、`right`、`bottom` 四个相机模组目录保存；`front` 包含海康彩色图和迈德威视左右目图，其他目录分别包含对应 Orbbec 的彩色与双 IR 图。
