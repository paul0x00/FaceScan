# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Common commands

Backend build and run from the repository root:

```bash
cmake -S . -B build
cmake --build build
./build/backend/facescan_backend 8080
```

Frontend install, develop, and build:

```bash
cd frontend && npm install
cd frontend && npm run dev -- --host 127.0.0.1
cd frontend && npm run build
cd frontend && npm run preview
```

There are currently no test scripts or test files in the repository. Use `npm run build` for frontend type-checking plus production build, and `cmake --build build` for backend compile verification.

## Runtime configuration and generated data

- Backend configuration is loaded from `config/app.json` if present, otherwise `config/app.example.json`.
- Config keys are `backendPort`, `database`, `imageRoot`, and `cameraMode`; defaults point at `data/db/facescan.sqlite3` and `data/images`.
- The backend accepts an optional port argument that overrides the config port.
- Runtime artifacts are intentionally ignored by git: `build/`, `frontend/dist/`, `frontend/node_modules/`, SQLite files under `data/db/`, and captured images under `data/images/`.

## Architecture overview

FaceScan is a first-phase MVP for a local face-scan workstation. The app has a C++ HTTP backend and a Vue 3 frontend; the frontend proxies `/api` requests to the backend during Vite development.

### Backend

- The backend is implemented in `backend/src/main.cpp` and built as `facescan_backend` by CMake.
- It uses Boost.Beast/Asio for HTTP, SQLite for local persistence, and a thread-per-connection accept loop.
- `Database` owns schema creation, lightweight migrations, seeding, and all patient/order/scan queries. Tables are `patient`, `orders`, and `scan_result`.
- `CameraManager` is currently a mock camera implementation. Preview frames are generated SVGs, and capture writes four SVG images for `left`, `front`, `right`, and `bottom` views into the configured image root.
- `App::handle` manually routes REST-style endpoints for login, health, patients, orders, camera start/stop/frame/capture, and export package/upload placeholders.
- JSON request parsing and response generation are handwritten helpers, not a JSON library.

### Frontend

- The frontend is a Vue 3 + TypeScript + Vite app in `frontend/` using Pinia, Vue Router, Element Plus, Axios, and lucide-vue-next.
- `frontend/src/main.ts` wires Vue, Pinia, Router, Element Plus, and global styles.
- `frontend/src/router/index.ts` defines the main routes and requires `localStorage` key `facescan.token` for every route except `/login`.
- `frontend/src/api/client.ts` is the typed Axios API layer. It uses base URL `/api` and attaches the stored token as a Bearer Authorization header.
- `frontend/src/stores/patients.ts` is the shared Pinia store for patient search results and selected patient state.
- `frontend/src/types/index.ts` mirrors the backend API shapes for patients, orders, scan results, and patient forms.

### Main user flow

1. `/login` stores a development token returned by `/api/login`.
2. `/browse` loads patients, filters by keyword/date, manages patient deletion, and displays each selected patient’s orders.
3. `/basic/:id?` creates or edits patient information. Creating a patient also creates the first order; editing can preserve an `orderId` query parameter.
4. `/shoot/:id` starts the mock camera, chooses an existing/latest order or creates one, refreshes four preview images from `/api/camera/frame`, and captures four SVGs through `/api/camera/capture`.
5. `/send/:id` lists scan records and calls MVP placeholder endpoints for local packaging and cloud upload.
6. The shared `StepHeader` represents the workflow as basic information, shooting, future 3D processing, and sending; the 3D processing step is intentionally not implemented in phase one.
