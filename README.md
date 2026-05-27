# FaceScan

面扫系统第一阶段 MVP 代码。

## 功能范围

- 登录
- 患者管理与订单创建
- 四路彩色相机预览（当前为模拟相机）
- 同步采图与图像保存
- SQLite 本地数据库
- 发送页的本地打包与云端上传接口占位

## 启动

后端：

```bash
cmake -S backend -B backend/build -G Ninja
cmake --build backend/build
./backend/build/facescan_backend 8080
```

后端测试：

```bash
cmake -S backend -B backend/build -G Ninja
cmake --build backend/build
ctest --test-dir backend/build --output-on-failure
```

说明：

- `cmake -S backend -B backend/build` 生成构建目录，并下载 GoogleTest。
- `cmake --build backend/build` 编译后端、核心库和测试程序。
- `ctest --test-dir backend/build --output-on-failure` 执行测试；失败时会打印具体失败原因。

前端：

```bash
cd frontend
npm install
npm run dev -- --host 127.0.0.1
```

前端默认访问 `http://127.0.0.1:8080/api`。

## 项目结构约定

- `backend/` 是完整后端工程，包含 CMake、源码、配置示例和运行数据目录。
- `backend/CMakeLists.txt` 是后端 CMake 入口。
- `backend/src/main.cpp` 只保留进程入口；HTTP 监听在 `backend/src/server/`，API 编排在 `backend/src/api/`。
- `backend/src/camera/` 保留相机服务门面和相机设备接口，当前默认设备仍是模拟相机，后续真实 SDK 可作为新的设备实现接入。
- `backend/src/algorithm/`、`backend/src/database/`、`backend/src/config/`、`backend/src/common/` 分别放点云、数据持久化、配置和通用工具。
- `backend/tests/` 放 GoogleTest 单元测试，当前覆盖 API 健康检查、相机模拟、SQLite 基础流程和 JSON 工具函数。
- `backend/config/app.example.json` 是后端配置示例；本地私有配置可写到 `backend/config/app.json`。
- `backend/data/` 是后端运行数据目录，SQLite 和采图文件都放在这里。
- `backend/build/` 是后端 CMake 生成目录，`compile_commands.json` 也只保留在这里。
- `frontend/` 是完整前端工程，包含 Vite、Vue 源码和前端依赖配置。
- 根目录只放仓库级文档、通用配置和前后端目录，不再放后端构建入口或后端运行数据。
