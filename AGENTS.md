# 仓库指南

## 项目结构与模块组织

FaceScan 分为 C++ 后端和 Vue/Vite 前端。后端源码位于 `backend/src/`，按职责划分为 `api/`、`server/`、`database/`、`camera/`、`algorithm/`、`config/`，公共工具放在 `common/`。后端测试位于 `backend/tests/`。运行配置示例位于 `backend/config/`；本地 `app.json` 不应提交。前端代码位于 `frontend/src/`，包括 `views/`、`components/`、`api/`、`stores/` 和 `types/`。平台脚本位于 `scripts/windows/` 和 `scripts/macos/`。

## 构建、测试与开发命令

- `.\scripts\windows\dev.cmd`：在 Windows 上安装/检查依赖、构建并启动前后端。
- `.\scripts\windows\stop-dev.cmd`：停止 Windows 本地开发服务。
- `make dev`：通过 `scripts/macos/dev.sh` 启动 macOS 开发环境。
- `cmake -S backend --preset windows && cmake --build backend/build --config Release`：使用 vcpkg/MSVC 构建后端。
- `cmake -S backend --preset macos && cmake --build backend/build`：使用 vcpkg/Ninja 构建后端。
- `ctest --test-dir backend/build --output-on-failure`：运行后端 GoogleTest 测试。
- `npm --prefix frontend run dev -- --host 127.0.0.1`：启动前端开发服务器。
- `npm --prefix frontend run build`：执行类型检查并构建前端。

## 编码风格与命名约定

后端使用 C++17。遵循现有风格：4 空格缩进，函数定义的大括号另起一行，使用 `facescan` 命名空间，函数和变量采用 lowerCamelCase，私有成员以 `_` 结尾，例如 `impl_`。头文件使用匹配的 `.hpp`，实现放在 `.cpp`。前端 TypeScript 和 Vue 文件使用 2 空格缩进、单引号、PascalCase 组件名，以及 lowerCamelCase 变量/函数名。

## 测试指南

后端测试通过 CTest 运行 GoogleTest。新增测试放在 `backend/tests/`，文件名以 `_test.cpp` 结尾；共享辅助逻辑放在 `test_utils.hpp` 或同等范围的文件中。提交后端变更前运行 `ctest --test-dir backend/build --output-on-failure`。前端当前未配置独立测试框架；Vue/TypeScript 变更至少运行 `npm --prefix frontend run build` 验证。

## 提交与 Pull Request 指南

近期提交采用 Conventional Commit 风格，例如 `docs: update setup and Orbbec SDK documentation` 和 `build: vendor Orbbec SDK binaries`。提交标题应简短、使用祈使语气，必要时添加作用域，例如 `chore(scripts): ...`。Pull Request 应说明变更内容，列出已执行的验证命令，关联相关 issue；涉及 UI 流程变更时附截图。

## 安全与配置提示

不要提交生成目录，例如 `backend/build/`、`frontend/dist/`、`frontend/node_modules/`、`.deps/` 或本地日志。私有运行配置保存在 `backend/config/app.json`；共享默认值写入 `backend/config/app.example.json`。Orbbec SDK 已放在 `backend/3rdParty/orbbec/`；更新二进制文件时保持现有平台目录结构。
