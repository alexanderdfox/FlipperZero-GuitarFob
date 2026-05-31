# Guitar Key Fob — build and install Flipper Zero FAP
#
# Requires: pip install ufbt
# Install over USB: connect Flipper and run `make install`

SHELL := /bin/bash
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

PYTHON_USER_BIN := $(HOME)/Library/Python/3.9/bin
export PATH := $(PYTHON_USER_BIN):$(PATH)

UFBT ?= ufbt

APP_DIR := $(ROOT)/guitar_key_fob
FAP := $(APP_DIR)/dist/guitar_key_fob.fap

.PHONY: all build install clean update deps help

.DEFAULT_GOAL := help

help:
	@echo "Guitar Key Fob — unified TX/RX Flipper app"
	@echo ""
	@echo "  make build      Build guitar_key_fob.fap"
	@echo "  make install    Build and upload over USB (ufbt launch)"
	@echo "  make clean      Remove local build outputs"
	@echo "  make update     Update uFBT SDK"
	@echo "  make deps       Install uFBT via pip"
	@echo ""
	@echo "Output: $(FAP)"

all: build

build: $(FAP)

$(FAP):
	@command -v $(UFBT) >/dev/null || { echo "ufbt not found. Run: make deps"; exit 1; }
	cd "$(APP_DIR)" && $(UFBT)

install: build
	@command -v $(UFBT) >/dev/null || { echo "ufbt not found. Run: make deps"; exit 1; }
	cd "$(APP_DIR)" && $(UFBT) launch

clean:
	rm -rf "$(APP_DIR)/dist" "$(APP_DIR)/.vscode" "$(APP_DIR)/.build"

update:
	@command -v $(UFBT) >/dev/null || { echo "ufbt not found. Run: make deps"; exit 1; }
	$(UFBT) update

deps:
	python3 -m pip install --user ufbt
	@echo ""
	@echo "If 'ufbt' is still not found, add Python user scripts to PATH:"
	@echo "  export PATH=\"$(PYTHON_USER_BIN):\$$PATH\""
