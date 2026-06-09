SHELL := /bin/bash

.PHONY: dev stop backend frontend

dev:
	./scripts/dev.sh

stop:
	./scripts/stop-dev.sh

backend:
	cmake -S backend -B backend/build -G Ninja
	cmake --build backend/build
	./backend/build/facescan_backend 8080

frontend:
	npm --prefix frontend run dev -- --host 127.0.0.1
