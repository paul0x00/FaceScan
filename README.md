# FaceScan

FaceScan 是一套面部扫描工作站原型系统，包含本地后端服务和 Vue 前端界面。项目围绕患者建档、订单管理、相机采集、点云重建、数据浏览与本地交付流程展开，支持模拟相机模式，也可在已链接 Orbbec SDK 时接入真实 Orbbec/Gemini 设备。

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

- 后端：C++11、Boost.Asio/Beast、SQLite3、ZLIB、CMake、GoogleTest。
- 相机：默认 `mock` 模拟设备；可配置 `orbbec` 或 `gemini215` 使用 Orbbec SDK。
- 前端：Vue 3、Vite、TypeScript、Pinia、Vue Router、Element Plus、lucide-vue-next、vtk.js。

## 快速启动

### 1. 构建并启动后端

```bash
cmake -S backend -B backend/build -G Ninja
cmake --build backend/build
./backend/build/facescan_backend 8080
```

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
cmake -S backend -B backend/build -G Ninja
cmake --build backend/build
ctest --test-dir backend/build --output-on-failure
```

说明：

- `cmake -S backend -B backend/build` 生成构建目录，并在首次配置时下载 GoogleTest。
- `cmake --build backend/build` 编译后端、核心库和测试程序。
- `ctest --test-dir backend/build --output-on-failure` 执行测试；失败时输出具体失败原因。

## 配置

后端配置示例位于 `backend/config/app.example.json`：

```json
{
  "backendPort": 8080,
  "database": "data/db/facescan.sqlite3",
  "imageRoot": "data/images",
  "cameraMode": "mock"
}
```

本地私有配置可保存为 `backend/config/app.json`，该文件已被 `.gitignore` 忽略。

- `backendPort`：后端默认监听端口。
- `database`：保留字段，当前 SQLite 固定保存到 `backend/data/db/facescan.sqlite3`。
- `imageRoot`：采集图片、点云、预览图等数据保存目录，支持通过设置页调整。
- `cameraMode`：`mock` 使用模拟相机；`orbbec` 或 `gemini215` 使用 Orbbec SDK 设备。

如果 CMake 未检测到 Orbbec SDK，而配置为 `orbbec` 或 `gemini215`，后端启动相机设备时会报错。保持 `mock` 可在没有硬件的情况下完整跑通主要流程。

## 项目结构

```text
.
├── backend/
│   ├── 3rdParty/          # Orbbec SDK 头文件和库文件
│   ├── config/            # 后端配置示例和本地配置
│   ├── data/              # SQLite、采集图片、点云和预览图
│   ├── src/
│   │   ├── algorithm/     # 点云重建算法
│   │   ├── api/           # HTTP API 编排
│   │   ├── camera/        # 模拟/真实相机设备封装
│   │   ├── common/        # 模型、文件、JSON、时间工具
│   │   ├── config/        # 应用配置读取与保存
│   │   ├── database/      # SQLite 持久化
│   │   └── server/        # Boost.Beast HTTP 服务
│   └── tests/             # GoogleTest 测试
├── frontend/
│   └── src/
│       ├── api/           # Axios API 客户端
│       ├── components/    # 通用组件
│       ├── router/        # 页面路由
│       ├── stores/        # Pinia 状态
│       ├── types/         # 前端类型定义
│       └── views/         # 业务页面
└── docs/                  # 设计文档和提交记录
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
- `backend/build/`、`frontend/node_modules/`、`frontend/dist/` 是生成目录，不进入版本库。
- `docs/记录.md` 用于记录每次提交的主要工作；`docs/面扫系统软件框架设计文档.md` 用于系统设计说明。
