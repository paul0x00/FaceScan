SHELL := /bin/bash

# VCPKG_ROOT 存在时走 vcpkg 工具链，否则回退到系统依赖
VCPKG_TOOLCHAIN := $(if $(VCPKG_ROOT),-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake,)

.PHONY: dev stop backend frontend

dev:
	./scripts/macos/dev.sh

stop:
	./scripts/macos/stop-dev.sh

backend:
	cmake -S backend -B backend/build -G Ninja $(VCPKG_TOOLCHAIN)
	cmake --build backend/build
	./backend/build/FaceScanBackend 8080

frontend:
	npm --prefix frontend run dev -- --host 127.0.0.1
