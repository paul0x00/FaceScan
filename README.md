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

```bash
cmake -S . -B build
cmake --build build
./build/backend/facescan_backend 8080
```

```bash
cd frontend
npm install
npm run dev -- --host 127.0.0.1
```

前端默认访问 `http://127.0.0.1:8080/api`。

