# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 常用命令

后端构建与运行（依赖由 vcpkg 管理，需设置 `VCPKG_ROOT` 指向 vcpkg 安装目录；从仓库根目录执行）。Windows 推荐 MSVC / Visual Studio 2022：

```powershell
cmake -S backend --preset windows
cmake --build backend/build --config Release
.\backend\build\Release\facescan_backend.exe 8080
```

macOS：

```bash
cmake -S backend --preset macos
cmake --build backend/build
./backend/build/facescan_backend 8080
```

无 vcpkg 时回退系统依赖（macOS 需 Homebrew 装 boost、sqlite3、zlib）；两种工具链切换前必须删除 `backend/build/` 重新配置：

```bash
cmake -S backend -B backend/build -G Ninja
cmake --build backend/build
```

后端测试（GoogleTest 优先取自 vcpkg，未用 vcpkg 工具链时回退 FetchContent 在线下载）：

```bash
ctest --test-dir backend/build --output-on-failure
```

按名称过滤运行单个测试（gtest 用例通过 `gtest_discover_tests` 逐个注册）：

```bash
ctest --test-dir backend/build -R DatabaseTest --output-on-failure
# 或直接用 gtest 过滤器运行测试可执行文件
./backend/build/facescan_tests --gtest_filter='DatabaseTest.*'
```

前端安装、开发、构建、类型检查：

```bash
cd frontend && npm install
cd frontend && npm run dev -- --host 127.0.0.1   # 开发服务器默认监听 5173，并把 /api 代理到 http://127.0.0.1:8080
cd frontend && npm run build                     # 类型检查（vue-tsc）+ 生产构建，产物输出到 frontend/dist/
cd frontend && npm run preview
```

整栈快捷命令（`Makefile` 封装了 `scripts/`）：

```bash
make dev     # 构建并启动后端与前端，pid 和日志写入 Log/
make stop    # 通过 scripts/stop-dev.sh 同时停止两端
make backend # 仅构建并运行后端
make frontend
```

注意：`scripts/dev.sh` 会**通过 `sudo` 以 root 身份**启动后端，因为真实 Orbbec 相机需要 USB 访问权限；日志与 pid 文件落在 `Log/`。`scripts/deduplicate-orders.sh` 是针对 SQLite 库的一次性 SQL 维护脚本。

后端拆分为 `facescan_core` 静态库与 `facescan_backend` 可执行文件，因此测试链接的是 `facescan_core` 而非进程入口。

## 依赖管理

- 通用第三方库（`boost-beast`、`boost-asio`、`sqlite3`、`zlib`、`gtest`）由 **vcpkg 清单模式**管理：`backend/vcpkg.json` 声明依赖并用 `builtin-baseline` 锁定版本；首次配置时自动安装到 `backend/build/vcpkg_installed/`。
- `backend/CMakePresets.json` 提供 `macos`（Ninja）、`windows`（VS 2022 x64）、`system`（不用 vcpkg）三个预设，前两者通过 `$env{VCPKG_ROOT}` 引用工具链文件。
- 新机器初始化 vcpkg：`git clone https://github.com/microsoft/vcpkg.git ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics`（Windows 用 `bootstrap-vcpkg.bat`），再设置环境变量 `VCPKG_ROOT`。
- **Orbbec SDK 是厂商闭源预编译包，不经 vcpkg 管理**：当前仓库已提交 `backend/3rdParty/orbbec/windows` 与 `backend/3rdParty/orbbec/macos`，Linux 目录预留。CMake 按平台自动探测，可用 `-DFACESCAN_ORBBEC_SDK_DIR` 覆盖。Windows 构建后会把 `bin/` 下的 dll/xml 自动复制到后端和测试可执行文件目录。

## 运行时配置与生成数据

- 配置优先从 `backend/config/app.json` 读取，不存在时回退到 `backend/config/app.example.json`。相对路径以 `backend/` 为基准解析。
- 配置键：`backendPort`、`database`、`imageRoot`、`cameraMode`。
  - `database` 目前是**保留字段**——代码在加载时用固定路径覆盖它（见 `app_config.cpp` 的 `fixedDatabasePath`），SQLite 始终落在 `backend/data/db/facescan.sqlite3`，配置里写的值不生效。
  - `imageRoot`（默认 `backend/data/images`）保存采集图片、点云和预览图，可在设置页调整。
  - `cameraMode` 取值为 `mock`（默认，无需硬件）、`orbbec` 或 `gemini215`。
- 可选的命令行端口参数会覆盖配置中的端口。
- 被 git 忽略的运行期产物：`backend/build/`、`frontend/dist/`、`frontend/node_modules/`、`backend/data/db/` 下的 SQLite 文件、`backend/data/images/` 下的采集数据、`backend/config/app.json`、`vcpkg_installed/` 以及 `Log/`。`backend/3rdParty/orbbec/windows` 与 `backend/3rdParty/orbbec/macos` 是例外，已随仓库提交。

## 架构概览

FaceScan 是一个本地化的面部扫描工作站：C++ HTTP 后端 + Vue 3 前端。Vite 开发期由前端把 `/api` 代理到后端。完整流水线已端到端实现——患者/订单管理、同步相机采集、**点云重建**、点云查看、本地打包（云端上传仍是占位实现）。

### 后端

- `backend/src/main.cpp` 仅是进程入口。`backend/src/server/http_server.*` 负责 Boost.Beast/Asio 的 HTTP 监听，采用每连接一线程的 accept 循环。`backend/src/api/app.*`（`App::handle`）负责全部路由与响应生成。
- **路由是 `App::handle` 中一条手写的 if 阶梯**，按 method + path 匹配；路径参数（id）用 substring/`atoi` 解析。JSON 的请求解析与响应生成是 `common/json_utils` 里**手写的辅助函数，没有使用 JSON 库**。
- 端点分组：`/api/login`、`/api/health`、`/api/settings`（外加 `open-data-root`、`select-data-root`、`validate-data-root`）、`/api/patients`（CRUD、`/orders`、`/scans`）、`/api/orders`（创建、删除、`/reconstruct`、`/open-folder`）、`/api/camera`（`start`、`stop`、`controls`、`frame`、`capture`）、`/api/pointcloud/file`、`/api/files/image`、`/api/export/package`、`/api/export/upload`。文件服务类端点会防御路径穿越，且要求路径位于 `imageRoot` 之下。
- `Database`（`database/`）负责建表、轻量列迁移（`ALTER TABLE ... ADD COLUMN`）、初始数据填充和所有查询。表：`patient`、`orders`、`scan_result`，并在 `orders(patient_id, order_no)` 上建有唯一索引，保证同一患者订单号唯一。
- `CameraManager` 是 `ICameraDevice` 的门面（`camera/camera_manager.*`）。`createCameraDevice` 依据 `cameraMode` 选择具体实现：
  - `MockCameraDevice`——默认；生成 SVG 预览帧和合成采集数据，无需硬件即可运行。
  - `OrbbecCameraDevice`——**仅在检测到 Orbbec SDK 时才编译**（见下方构建开关）；驱动真实 Orbbec/Gemini 设备，`orbbec` 与 `gemini215` 两种模式共用该实现。
  - 一次 `capture` 产出一组 `SynchronizedCaptureFrames`：彩色图 + 左红外 + 右红外 + 清单（manifest）+ 纹理路径（不再是早期的四视图 SVG）。
- `ColorPointCloudSdk`（`algorithm/color_point_cloud_sdk.*`）从采集图重建带颜色的 PLY 点云，返回点数与包围盒，通过 `/api/orders/{id}/reconstruct` 端点调用。
- **Orbbec SDK 是可选依赖，在配置阶段检测**（`backend/CMakeLists.txt`）：按平台在 `backend/3rdParty/orbbec/{macos,windows,linux}/` 下查找 `include/libobsensor/ObSensor.hpp` 与对应库文件（macOS/Linux 为 `lib/libOrbbecSDK*`，Windows 为 `lib/OrbbecSDK*.lib`），找到则定义 `FACESCAN_WITH_ORBBEC` 并链接 SDK，可用 `-DFACESCAN_ORBBEC_SDK_DIR` 指定其他位置。缺失时硬件设备类不参与编译，运行期 `orbbec`/`gemini215` 模式不可用——无硬件时保持 `mock` 即可跑通完整流程。核心依赖（Boost 头文件、SQLite3、ZLIB）由 vcpkg 提供，也兼容系统安装。

### 前端

- Vue 3 + TypeScript + Vite，搭配 Pinia、Vue Router、Element Plus、Axios、lucide-vue-next 以及 **vtk.js**（点云渲染）。`frontend/src/main.ts` 完成整体装配；`frontend/src/styles.css` 为全局样式。
- `frontend/src/router/index.ts` 定义路由，除 `/login` 外的每条路由都要求 `localStorage` 中存在 `facescan.token`。
- `frontend/src/api/client.ts` 是带类型的 Axios 封装层：基础路径 `/api`，将存储的 token 作为 Bearer 头附加。
- `frontend/src/stores/patients.ts` 保存共享的患者搜索/选择状态。`frontend/src/types/index.ts` 与后端 API 形状保持一致。
- 页面（Views）：`LoginView`、`BrowseView`、`BasicInfoView`、`ShootView`、`PointCloudView`、`SendView`、`SettingsView`。共享组件包括 `StepHeader`（工作流步骤条）、`FormPanel`、`LogoMark`、`WindowIcons`。

### 主用户流程

1. `/login` 保存来自 `/api/login` 的开发 token（开发表单默认 `admin / admin`）。
2. `/browse` 列出患者，按关键字/日期范围筛选，展示缩略图，管理患者/订单删除，并列出选中患者的订单。
3. `/basic/:id?` 创建或编辑患者（创建时同时创建首个订单；编辑时可通过 `orderId` 查询参数保留订单上下文）。
4. `/shoot/:id` 启动相机、选择/创建订单、通过 `/api/camera/frame` 刷新预览、调整相机参数，并执行同步 `/api/camera/capture` 采图。
5. `/pointcloud/:id` 触发/加载点云重建，并用 vtk.js 渲染 PLY。
6. `/send/:id` 列出扫描记录，调用本地打包以及占位的云端上传。
7. `/settings` 校验、保存并打开本地数据目录。
8. `StepHeader` 将工作流建模为：基础信息 → 拍摄 → 点云 → 发送。

## 项目约定

项目规则位于 `.claude/rules/`，对所有改动生效：

- `code-style.md`——TypeScript 严格模式，禁用 `any`，函数不超过 40 行，优先 `const`，导入顺序（标准库 → 三方包 → 本地模块），所有 export 的函数/类型需 JSDoc 注释，禁止 `console.log`（使用项目 logger）。
- `api-conventions.md`——RESTful 资源名用复数，统一响应格式 `{ data, error, meta }`，错误码遵循 HTTP 标准语义，每个接口都需在 OpenAPI 中声明，分页统一用 `page` / `page_size`。

设计文档与变更记录在 `docs/` 下（`面扫系统软件框架设计文档.md`、`接口清单与页面对应关系.md`、`记录.md`，其中 `记录.md` 用于记录每次提交的主要工作）；UI 参考稿在 `design-mockups/`。
