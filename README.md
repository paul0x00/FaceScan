# FaceScan

FaceScan 是一套面部扫描工作站原型系统，包含本地后端服务和 Vue 前端界面。项目围绕患者建档、订单管理、相机采集、点云重建、数据浏览与本地交付流程展开，支持模拟相机、单台 Orbbec/Gemini，以及由 3 台 Orbbec、1 台海康彩色相机和 1 台迈德威视双目相机组成的多相机采集模组。

## 当前能力

- 本地登录与路由守卫，默认开发账号表单为 `admin / admin`。
- 患者建档、编辑、删除，以及患者编号和订单编号管理。
- 患者浏览页支持关键字、创建日期范围、缩略图和订单列表查看。
- 订单创建、删除、打开订单文件夹，以及订单扫描记录展示。
- 相机预览、同步采图、相机参数读取/调整和截图交互。
- 彩色图、双目红外数据的点云重建，并提供 PLY 文件和预览图。
- 点云查看页基于 `vtk.js` 展示重建结果。
- 设置页支持数据目录校验、保存和打开本地数据目录。
- 本地打包交付流程，以及预留的云端上传接口。
- 后端 GoogleTest 覆盖 API、数据库、相机、JSON 工具和点云算法等模块。

## 技术栈

- 后端：C++17、Boost.Asio/Beast、SQLite3、ZLIB、CMake、GoogleTest（依赖统一由 vcpkg 管理）。
- 相机：默认 `mock` 模拟设备；可配置 `orbbec`/`gemini215` 使用单台 Orbbec，或使用 `multi_camera` 驱动 3 台 Orbbec、1 台海康彩色相机和 1 台迈德威视双目相机。
- 前端：Vue 3、Vite、TypeScript、Pinia、Vue Router、Element Plus、lucide-vue-next、vtk.js。

## 快速启动

### Windows 一键启动（推荐）

从 GitHub 克隆仓库后，在项目根目录执行：

```powershell
.\scripts\windows\dev.cmd
```

脚本会自动下载/初始化 vcpkg、安装前端 npm 依赖、构建后端并启动前后端。首次运行需要联网，并要求本机已有 Git、CMake、Node.js LTS、Visual Studio 2022 C++ Build Tools。若希望脚本尝试用 winget 自动安装这些基础工具，可执行：

```powershell
.\scripts\windows\dev.cmd -InstallTools
```

启动成功后访问 `http://127.0.0.1:5173`。停止服务：

```powershell
.\scripts\windows\stop-dev.cmd
```

### 0. 准备依赖（vcpkg，一次性）

```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics    # Windows 执行 .\bootstrap-vcpkg.bat
export VCPKG_ROOT="$HOME/vcpkg"               # 建议写入 shell 配置；Windows 设置系统环境变量
```

第三方依赖（Boost.Beast/Asio、SQLite3、ZLIB、GoogleTest）声明在 `backend/vcpkg.json`，由 `builtin-baseline` 锁定版本。首次配置时 vcpkg 会把依赖安装到 `backend/build/vcpkg_installed/`，该目录是本地构建产物，不进入版本库。

Orbbec、海康 MVS 和迈德威视 SDK 均为厂商闭源包，不经 vcpkg 管理。当前仓库已提交 Windows 与 macOS 平台的 Orbbec SDK；海康 MVS 和迈德威视 SDK 需要按 `backend/3rdParty/README.md` 的布局手工放置。缺少硬件 SDK 时仍可使用 `mock` 模式。

### 1. 构建并启动后端

Windows 推荐使用 MSVC / Visual Studio 2022 工具链：

```powershell
$env:VCPKG_ROOT = "C:\src\vcpkg"
cmake -S backend --preset windows
cmake --build backend/build --config Release
.\backend\build\Release\FaceScanBackend.exe 8080
```

macOS 使用 vcpkg + Ninja 预设：

```bash
export VCPKG_ROOT="$HOME/vcpkg"
cmake -S backend --preset macos
cmake --build backend/build
./backend/build/FaceScanBackend 8080
```

首次配置会自动安装 vcpkg 依赖（需联网，约几分钟）。未安装 vcpkg 时可回退系统依赖（macOS 需 Homebrew 安装 `boost`、`sqlite3`、`zlib`）：`cmake -S backend -B backend/build -G Ninja`。注意两种工具链或生成器之间切换需先删除 `backend/build/`。

启动成功后，后端监听 `http://127.0.0.1:8080`。如果不传端口参数，会读取 `backend/config/app.json` 或 `backend/config/app.example.json` 中的 `backendPort`。

### 2. 启动前端

```bash
cd frontend
npm install
npm run dev -- --host 127.0.0.1
```

前端默认访问 `http://127.0.0.1:5173`，开发服务器会把 `/api` 代理到 `http://127.0.0.1:8080`。

### 3. 生产构建前端

```bash
cd frontend
npm run build
```

构建产物输出到 `frontend/dist/`。

## 后端测试

```bash
cmake -S backend --preset macos
cmake --build backend/build
ctest --test-dir backend/build --output-on-failure
```

说明：

- GoogleTest 优先来自 vcpkg；未使用 vcpkg 工具链时回退 FetchContent 在线下载。
- `cmake --build backend/build` 编译主程序、各后端动态模块和测试程序。
- `ctest --test-dir backend/build --output-on-failure` 执行测试；失败时输出具体失败原因。

## 配置

后端配置示例位于 `backend/config/app.example.json`：

```json
{
  "backendPort": 8080,
  "database": "data/db/facescan.sqlite3",
  "imageRoot": "data/images",
  "cameraMode": "mock",
  "multiCameraTriggerWorkflow": "orbbec_parallel_then_module4",
  "orbbecLeftSerial": "",
  "orbbecRightSerial": "",
  "orbbecBottomSerial": "",
  "hikvisionFrontSerial": "",
  "mindvisionStereoSerial": "",
  "cameraTriggerTimeoutMs": 1000
}
```

本地私有配置可保存为 `backend/config/app.json`，该文件已被 `.gitignore` 忽略。

- `backendPort`：后端默认监听端口。
- `database`：保留字段，当前 SQLite 固定保存到 `backend/data/db/facescan.sqlite3`。
- `imageRoot`：采集图片、点云、预览图等数据保存目录，支持通过设置页调整。
- `cameraMode`：`mock` 使用模拟相机；`orbbec`/`gemini215` 使用单台 Orbbec；`multi_camera` 使用三台 Orbbec、海康彩色相机和迈德威视双目相机。
- `multiCameraTriggerWorkflow`：`orbbec_parallel_then_module4`（默认）先并行采集三台 Orbbec，再同步触发海康与迈德威视；`sequential_modules` 依次采集三台 Orbbec，再触发第四模组。
- 五个 `*Serial` 字段：优先按序列号绑定角色；为空或找不到时按 SDK 枚举顺序兜底，且三台 Orbbec 不会重复绑定。
- `cameraTriggerTimeoutMs`：厂商设备单步触发等待超时，单位毫秒。

如果 CMake 未检测到相应 SDK，硬件模式启动时会返回明确错误。`multi_camera` 当前面向 Windows 厂商 SDK；保持 `mock` 可在没有硬件的情况下完整跑通主要流程。

### 多相机拍摄与保存

拍摄页四路预览固定映射如下：左、右、下分别显示三台 Orbbec 的彩色流，正面显示海康彩色流。拍摄页的自动曝光、曝光时间和增益会统一下发到五台设备；其中 Orbbec 会同时设置彩色与 IR，统一曝光范围为 `1–100 ms`，统一增益范围为 `1–20 倍`。

每次同步拍摄均保存为 BMP，并在订单目录下按正面、左侧、右侧、底部四个相机模组拆分。正面模组包含海康彩色图和迈德威视双目图，其余三个模组分别包含对应 Orbbec 的彩色与双 IR 图：

```text
<订单目录>/
├── front/
│   ├── <订单号>_color.bmp       # 海康彩色
│   ├── <订单号>_left.bmp        # 迈德威视左目
│   └── <订单号>_right.bmp       # 迈德威视右目
├── left/
│   ├── <订单号>_color.bmp       # 左 Orbbec 彩色
│   ├── <订单号>_left_ir.bmp     # 左 Orbbec 左 IR
│   └── <订单号>_right_ir.bmp    # 左 Orbbec 右 IR
├── right/
│   ├── <订单号>_color.bmp       # 右 Orbbec 彩色
│   ├── <订单号>_left_ir.bmp     # 右 Orbbec 左 IR
│   └── <订单号>_right_ir.bmp    # 右 Orbbec 右 IR
├── bottom/
│   ├── <订单号>_color.bmp       # 下 Orbbec 彩色
│   ├── <订单号>_left_ir.bmp     # 下 Orbbec 左 IR
│   └── <订单号>_right_ir.bmp    # 下 Orbbec 右 IR
└── <订单号>_multi_camera_capture.json
```

一次完整触发共保存 12 张 BMP。接口和数据根反扫会将前四张固定排序为：左 Orbbec 彩色、海康正面彩色、右 Orbbec 彩色、下 Orbbec 彩色；点云重建优先使用迈德威视左右图恢复几何，并使用海康正面彩色图贴纹理。历史 PPM/PGM 数据仍可读取。

## 后端模块化产物

后端除进程入口外均按职责构建为动态模块。Windows 在 `backend/build/Release/` 生成 DLL 和对应导入库，macOS 在 `backend/build/` 生成 dylib：

| CMake 目标 | Windows 产物 | 职责 |
|---|---|---|
| `FaceScanCamera` | `FaceScanCamera.dll` | 相机门面、模拟设备和厂商 SDK 适配 |
| `FaceScanConfig` | `FaceScanConfig.dll` | 配置默认值、加载和保存 |
| `FaceScanReconstruction` | `FaceScanReconstruction.dll` | 点云重建和编辑 |
| `FaceScanService` | `FaceScanService.dll` | HTTP API、监听和业务编排 |
| `FaceScanDatabase` | `FaceScanDatabase.dll` | SQLite 持久化和数据目录同步 |
| `facescan_common` | 静态库 | 各动态模块内部使用的文件、JSON、BMP 和时间工具，不作为运行时模块发布 |
| `FaceScanBackend` | `FaceScanBackend.exe` | 加载配置、解析启动参数并调用服务模块 |

运行时依赖方向为：`FaceScanBackend → FaceScanConfig + FaceScanService → FaceScanCamera + FaceScanReconstruction + FaceScanDatabase`。Windows 构建会把已检测到的 Orbbec、海康和迈德威视 SDK 运行时文件复制到 `FaceScanCamera.dll` 所在的输出目录。

## 项目结构

```text
.
├── backend/
│   ├── 3rdParty/          # 厂商 SDK（Orbbec 已含 Windows/macOS，其他见其 README.md）
│   ├── config/            # 后端配置示例和本地配置
│   ├── data/              # SQLite、采集图片、点云和预览图
│   ├── src/
│   │   ├── FaceScanBackend/         # 后端进程入口
│   │   ├── FaceScanCamera/          # 模拟/真实相机设备封装
│   │   ├── FaceScanConfig/          # 应用配置读取与保存
│   │   ├── FaceScanDatabase/        # SQLite 持久化
│   │   ├── FaceScanReconstruction/  # 点云重建与编辑
│   │   ├── FaceScanService/         # HTTP API、监听和业务编排
│   │   └── common/                  # 模型、文件、JSON、时间工具
│   └── tests/             # GoogleTest 测试
├── frontend/
│   └── src/
│       ├── api/           # Axios API 客户端
│       ├── components/    # 通用组件
│       ├── router/        # 页面路由
│       ├── stores/        # Pinia 状态
│       ├── types/         # 前端类型定义
│       └── views/         # 业务页面
└── scripts/               # 按平台划分的开发启动、停止和数据维护脚本
```

## 常用页面

- `/login`：本地登录。
- `/browse`：患者和订单浏览，支持关键字和日期范围筛选。
- `/basic/:id?`：患者建档或编辑。
- `/shoot/:id`：拍摄、相机控制和截图。
- `/pointcloud/:id`：点云查看。
- `/send/:id`：本地打包和上传占位流程。
- `/settings`：数据目录和系统配置。

## 数据与版本控制约定

- `backend/data/` 保存运行期数据，只保留目录占位文件进入版本库。
- `backend/config/app.json` 是本地私有配置，不进入版本库。
- `backend/build/`、`frontend/node_modules/`、`frontend/dist/`、`vcpkg_installed/` 是生成目录，不进入版本库。
- `backend/3rdParty/orbbec/windows` 与 `backend/3rdParty/orbbec/macos` 已进入版本库，用于开箱即用地链接 Orbbec SDK；后续替换 SDK 时应保持相同目录布局。
- `Log/` 是本地开发日志目录，不进入版本库。
