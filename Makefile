# Droidspaces v5 - Build system
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

BINARY_NAME = droidspaces
SRC_DIR     = src
OUT_DIR     = output

# Get version from header
VERSION := $(shell grep "DS_VERSION" $(SRC_DIR)/droidspace.h | awk '{print $$3}' | tr -d '"')

# Parallel jobs - use all available CPU cores
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

# Verbose control - V=1 shows full commands, V=0 (default) shows kernel-style short logs
V ?= 0
ifeq ($(V),1)
  Q       =
  msg_cc  =
  msg_ld  =
  msg_str =
else
  Q       = @
  msg_cc  = @printf "  CC      %s\n" $<
  msg_ld  = @printf "  LD      %s\n" $@
  msg_str = @printf "  STRIP   %s\n" $@
endif

# Source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/utils.c \
       $(SRC_DIR)/android.c \
       $(SRC_DIR)/seccomp.c \
       $(SRC_DIR)/mount.c \
       $(SRC_DIR)/cgroup.c \
       $(SRC_DIR)/network.c \
       $(SRC_DIR)/terminal.c \
       $(SRC_DIR)/console.c \
       $(SRC_DIR)/pid.c \
       $(SRC_DIR)/boot.c \
       $(SRC_DIR)/config.c \
       $(SRC_DIR)/container.c \
       $(SRC_DIR)/environment.c \
       $(SRC_DIR)/documentation.c \
       $(SRC_DIR)/hardware.c \
       $(SRC_DIR)/ds_iptables.c \
       $(SRC_DIR)/ds_netlink.c \
       $(SRC_DIR)/ds_dhcp.c \
       $(SRC_DIR)/daemon.c \
       $(SRC_DIR)/check.c

# Compiler flags - hardened warning set, all warnings are errors
CFLAGS  = -Wall -Wextra -Wpedantic -Werror -O2 -flto=auto -std=gnu99 -I$(SRC_DIR) -no-pie -pthread
CFLAGS += -Wformat=2 -Wformat-security -Wformat-overflow=2 -Wformat-truncation=2
CFLAGS += -Wnull-dereference -Wcast-qual -Wlogical-op -Wshadow -Wdouble-promotion -Wundef
CFLAGS += -Wduplicated-cond -Wduplicated-branches -Wimplicit-fallthrough=3
CFLAGS += -fstack-protector-strong
LDFLAGS = -static -no-pie -flto=auto -pthread
LIBS    = -lutil

# Auto-detect architecture from compiler
ARCH := $(shell $(CC) -dumpmachine 2>/dev/null | cut -d'-' -f1 | \
        sed 's/x86_64/x86_64/; s/aarch64/aarch64/; s/i686/x86/; \
             s/armv7l/armhf/; s/^arm/armhf/; s/unknown/x86_64/' || echo "x86_64")

# Per-arch object directory - prevents collisions when building multiple archs
OBJ_DIR = $(OUT_DIR)/.obj/$(ARCH)
OBJS    = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Cross-compiler helper
HOME_VAR := $(shell echo $$HOME)
find-cc = $(shell \
	if [ -n "$(MUSL_CROSS)" ] && [ -f "$(MUSL_CROSS)/$(1)-gcc" ]; then \
		echo "$(MUSL_CROSS)/$(1)-gcc"; \
	elif [ -f "$(HOME_VAR)/toolchains/$(1)-cross/bin/$(1)-gcc" ]; then \
		echo "$(HOME_VAR)/toolchains/$(1)-cross/bin/$(1)-gcc"; \
	elif command -v $(1)-gcc >/dev/null 2>&1; then \
		echo "$(1)-gcc"; \
	elif [ -d "/opt/cross/bin" ] && [ -f "/opt/cross/bin/$(1)-gcc" ]; then \
		echo "/opt/cross/bin/$(1)-gcc"; \
	else \
		echo ""; \
	fi)

.PHONY: all help clean native x86_64 aarch64 armhf x86 riscv64 all-build tarball all-tarball debug-hardened

all: help

help:
	@echo "Droidspaces v$(VERSION) Build System"
	@echo ""
	@echo "Build for specific architecture:"
	@echo "  make native    - Build for current architecture using musl-gcc"
	@echo "  make x86_64    - Build for 64-bit x86"
	@echo "  make aarch64   - Build for 64-bit ARM"
	@echo "  make armhf     - Build for 32-bit ARM (hard-float)"
	@echo "  make x86       - Build for 32-bit x86"
	@echo "  make riscv64   - Build for 64-bit RISC-V"
	@echo ""
	@echo "Advanced targets:"
	@echo "  make all-build   - Build for all supported architectures"
	@echo "  make tarball     - Create tarball for current binary"
	@echo "  make all-tarball - Build all and create unified distribution"
	@echo ""
	@echo "Options:"
	@echo "  V=1            - Show full compiler commands"
	@echo ""
	@echo "Other:"
	@echo "  make clean     - Remove build artifacts"
	@echo "  make debug-hardened - Build with ASan/UBSan/LSan to find bugs"

$(OUT_DIR):
	$(Q)mkdir -p $(OUT_DIR)

$(OBJ_DIR):
	$(Q)mkdir -p $(OBJ_DIR)

# Compile each source file to an object - runs in parallel via -j$(NPROC)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(msg_cc)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Link step
$(BINARY_NAME): $(OBJS) | $(OUT_DIR)
	$(msg_ld)
	$(Q)$(CC) $(OBJS) -o $(OUT_DIR)/$(BINARY_NAME) $(LDFLAGS) $(LIBS)
	@if [ -z "$(NO_STRIP)" ]; then \
		STRIP_CMD=$$(echo $(CC) | sed 's/-gcc$$/-strip/; s/gcc$$/strip/'); \
		if command -v $$STRIP_CMD >/dev/null 2>&1 || [ -f "$$STRIP_CMD" ]; then \
			$(if $(filter 0,$(V)),printf "  STRIP   $(OUT_DIR)/$(BINARY_NAME)\n";) \
			$$STRIP_CMD -s $(OUT_DIR)/$(BINARY_NAME) 2>/dev/null || true; \
		fi; \
	fi
	@echo "[+] Built: $(OUT_DIR)/$(BINARY_NAME)"

# Build targets
NATIVE_ARCH_RAW := $(shell $(CC) -dumpmachine 2>/dev/null | cut -d'-' -f1 | sed 's/i.86/i686/')
ifeq ($(NATIVE_ARCH_RAW),x86_64)
  NATIVE_TARGET := x86_64-linux-musl
else ifeq ($(NATIVE_ARCH_RAW),aarch64)
  NATIVE_TARGET := aarch64-linux-musl
else ifeq ($(NATIVE_ARCH_RAW),i686)
  NATIVE_TARGET := i686-linux-musl
else ifeq ($(NATIVE_ARCH_RAW),riscv64)
  NATIVE_TARGET := riscv64-linux-musl
else
  NATIVE_TARGET := x86_64-linux-musl
endif

NATIVE_CC := $(call find-cc,$(NATIVE_TARGET))

native:
	@if [ -n "$(NATIVE_CC)" ]; then \
		$(MAKE) -j$(NPROC) $(BINARY_NAME) CC=$(NATIVE_CC); \
	else \
		echo "Error: Musl toolchain for $(NATIVE_TARGET) not found."; \
		echo "Please run: ./install-musl.sh $(shell echo $(NATIVE_TARGET) | cut -d'-' -f1 | sed 's/i686/x86/')"; \
		exit 1; \
	fi

x86_64:
	@CROSS_CC="$(call find-cc,x86_64-linux-musl)"; \
	if [ -n "$$CROSS_CC" ]; then $(MAKE) -j$(NPROC) $(BINARY_NAME) CC=$$CROSS_CC; \
	else echo "Error: x86_64-linux-musl-gcc not found. Run ./install-musl.sh x86_64"; exit 1; fi

aarch64:
	@CROSS_CC="$(call find-cc,aarch64-linux-musl)"; \
	if [ -n "$$CROSS_CC" ]; then $(MAKE) -j$(NPROC) $(BINARY_NAME) CC=$$CROSS_CC; \
	else echo "Error: aarch64-linux-musl-gcc not found. Run ./install-musl.sh aarch64"; exit 1; fi

armhf:
	@CROSS_CC="$(call find-cc,arm-linux-musleabihf)"; \
	if [ -z "$$CROSS_CC" ]; then CROSS_CC="$(call find-cc,armv7l-linux-musleabihf)"; fi; \
	if [ -n "$$CROSS_CC" ]; then $(MAKE) -j$(NPROC) $(BINARY_NAME) CC=$$CROSS_CC; \
	else echo "Error: arm-linux-musleabihf-gcc not found. Run ./install-musl.sh armhf"; exit 1; fi

x86:
	@CROSS_CC="$(call find-cc,i686-linux-musl)"; \
	if [ -n "$$CROSS_CC" ]; then $(MAKE) -j$(NPROC) $(BINARY_NAME) CC=$$CROSS_CC; \
	else echo "Error: i686-linux-musl-gcc not found. Run ./install-musl.sh x86"; exit 1; fi

riscv64:
	@CROSS_CC="$(call find-cc,riscv64-linux-musl)"; \
	if [ -n "$$CROSS_CC" ]; then $(MAKE) -j$(NPROC) $(BINARY_NAME) CC=$$CROSS_CC; \
	else echo "Error: riscv64-linux-musl-gcc not found. Run ./install-musl.sh riscv64"; exit 1; fi

debug-hardened: $(OUT_DIR)
	@echo "[*] Building hardened debug binary..."
	@mkdir -p $(OUT_DIR)/.obj/debug
	$(Q)$(CC) $(SRCS) -o $(OUT_DIR)/$(BINARY_NAME)-hardened \
		-I$(SRC_DIR) -g3 -O1 -pthread -lutil \
		-fsanitize=address -fsanitize=undefined -fsanitize=leak \
		-fstack-protector-strong -D_FORTIFY_SOURCE=2 \
		-Wall -Wextra -Wno-unused-parameter
	@echo "[+] Hardened binary built: $(OUT_DIR)/$(BINARY_NAME)-hardened"
	@echo "[!] Note: Run this on a standard Linux host (not static/musl) for best results."

ANDROID_ASSETS_DIR = Android/app/src/main/assets/binaries

sync-android:
	@if [ -d "$(ANDROID_ASSETS_DIR)" ]; then \
		cp -r $(OUT_DIR)/* $(ANDROID_ASSETS_DIR)/ && echo "[+] Synced binaries to Android assets"; \
	fi

# all-build: fail immediately if any architecture fails - no || fallback
all-build:
	@echo "[*] Building for all architectures ($(NPROC) jobs each)..."
	@rm -rf $(OUT_DIR)
	@$(MAKE) --no-print-directory x86_64  && mv $(OUT_DIR)/$(BINARY_NAME) $(OUT_DIR)/$(BINARY_NAME)-x86_64
	@$(MAKE) --no-print-directory aarch64 && mv $(OUT_DIR)/$(BINARY_NAME) $(OUT_DIR)/$(BINARY_NAME)-aarch64
	@$(MAKE) --no-print-directory armhf   && mv $(OUT_DIR)/$(BINARY_NAME) $(OUT_DIR)/$(BINARY_NAME)-armhf
	@$(MAKE) --no-print-directory x86     && mv $(OUT_DIR)/$(BINARY_NAME) $(OUT_DIR)/$(BINARY_NAME)-x86
	@$(MAKE) --no-print-directory riscv64 && mv $(OUT_DIR)/$(BINARY_NAME) $(OUT_DIR)/$(BINARY_NAME)-riscv64
	@$(MAKE) --no-print-directory sync-android
	@echo "[+] All architectures built successfully in $(OUT_DIR)/"

tarball:
	@if [ ! -f $(OUT_DIR)/$(BINARY_NAME) ]; then \
		echo "Error: $(OUT_DIR)/$(BINARY_NAME) not found. Build it first."; \
		exit 1; \
	fi
	@DETECTED_ARCH=$$(file $(OUT_DIR)/$(BINARY_NAME) | grep -oP '(x86-64|ARM aarch64|UCB RISC-V|i386|ARM)' | sed 's/x86-64/amd64/; s/ARM aarch64/arm64/; s/UCB RISC-V/riscv64/; s/i386/x86/; s/ARM/armhf/'); \
	TARBALL="$(BINARY_NAME)-$$DETECTED_ARCH-static-v$(VERSION).tar.gz"; \
	echo "[*] Creating $$TARBALL..."; \
	tar -czf $$TARBALL -C $(OUT_DIR) $(BINARY_NAME); \
	echo "[+] Created: $$TARBALL ($$(du -h $$TARBALL | cut -f1))"

all-tarball: all-build
	@DATE=$$(date +%Y-%m-%d); \
	TARBALL="$(BINARY_NAME)-v$(VERSION)-$$DATE.tar.gz"; \
	ROOT_DIR="$(BINARY_NAME)-v$(VERSION)"; \
	TEMP_DIR="/tmp/droidspaces-tarball-$$$$"; \
	mkdir -p $$TEMP_DIR/$$ROOT_DIR; \
	for arch in x86_64 aarch64 armhf x86 riscv64; do \
		if [ -f $(OUT_DIR)/$(BINARY_NAME)-$$arch ]; then \
			mkdir -p $$TEMP_DIR/$$ROOT_DIR/$$arch; \
			cp $(OUT_DIR)/$(BINARY_NAME)-$$arch $$TEMP_DIR/$$ROOT_DIR/$$arch/$(BINARY_NAME); \
		fi; \
	done; \
	echo "[*] Creating unified distribution: $$TARBALL..."; \
	tar -czf $$TARBALL -C $$TEMP_DIR $$ROOT_DIR; \
	rm -rf $$TEMP_DIR; \
	$(MAKE) --no-print-directory sync-android; \
	echo "[+] Created: $$TARBALL ($$(du -h $$TARBALL | cut -f1))"

clean:
	@rm -rf $(OUT_DIR) $(BINARY_NAME)-*.tar.gz
	@echo "[+] Cleaned build artifacts"
