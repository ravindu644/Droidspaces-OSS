# android-bridge - Build system, ported from Droidspaces
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

BINARY_NAME = android-bridge
SRC_DIR     = src
OUT_DIR     = output

# Default compiler. Do not allow recursive builds to accidentally get empty CC.
CC ?= cc

# AB_VERSION in bridge.h is the one place the version lives; `make bump` rewrites it
VERSION := $(shell grep "define AB_VERSION" $(SRC_DIR)/include/bridge.h | awk '{print $$3}' | tr -d '"')

NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

# V=1 shows full commands, V=0 (default) shows kernel-style short logs
V ?= 0

ifeq ($(V),1)
  Q      =
  msg_cc =
  msg_ld =
else
  Q      = @
  msg_cc = @printf "  CC      %s\n" $<
  msg_ld = @printf "  LD      %s\n" $@
endif

# Every source listed by hand: a stray .c in the tree must not get linked in
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/proto.c \
       $(SRC_DIR)/daemon.c \
       $(SRC_DIR)/client.c

# Hardened warning set, all warnings are errors
CFLAGS  = -Wall -Wextra -Wpedantic -Werror -O2 -flto=auto -std=gnu99 -I$(SRC_DIR)/include -no-pie -pthread
CFLAGS += -Wformat=2 -Wformat-security -Wformat-overflow=2 -Wformat-truncation=2
CFLAGS += -Wnull-dereference -Wcast-qual -Wlogical-op -Wshadow -Wdouble-promotion -Wundef
CFLAGS += -Wduplicated-cond -Wduplicated-branches -Wimplicit-fallthrough=3
CFLAGS += -fstack-protector-strong
# A header edit rebuilds every object that includes it
CFLAGS += -MMD -MP
# LTO raises some warnings only at link time, and they are failures there too
LDFLAGS = -static -no-pie -flto=auto -pthread -Werror
ifeq ($(NO_STRIP),)
  LDFLAGS += -s
endif
LIBS    = -lutil

ARCH := $(shell $(CC) -dumpmachine 2>/dev/null | cut -d'-' -f1 | \
        sed 's/i.86/x86/; s/armv7l/armhf/; s/^arm.*/armhf/; s/unknown/x86_64/' || echo "x86_64")

# Objects and the binary live per arch so the cross builds do not collide
OBJ_DIR = $(OUT_DIR)/.obj/$(ARCH)
OBJS    = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
-include $(OBJS:.o=.d)

# The link target is a real file, so an unchanged arch is a no-op
BINARY = $(OUT_DIR)/$(BINARY_NAME)-$(ARCH)

ARCHES = x86_64 aarch64 armhf x86 riscv64

find-cc = $(shell \
	if [ -n "$(MUSL_CROSS)" ] && [ -f "$(MUSL_CROSS)/$(1)-gcc" ]; then \
		echo "$(MUSL_CROSS)/$(1)-gcc"; \
	elif [ -f "$(HOME)/toolchains/$(1)-cross/bin/$(1)-gcc" ]; then \
		echo "$(HOME)/toolchains/$(1)-cross/bin/$(1)-gcc"; \
	elif command -v $(1)-gcc >/dev/null 2>&1; then \
		echo "$(1)-gcc"; \
	fi)

.PHONY: all help clean native $(ARCHES) all-build all-tarball module debug-hardened format bump test

all: help

help:
	@echo "android-bridge v$(VERSION) Build System"
	@echo ""
	@echo "Build (static, musl):"
	@echo "  make native    - Build for this machine's architecture"
	@echo "  make x86_64    - Build for 64-bit x86"
	@echo "  make aarch64   - Build for 64-bit ARM"
	@echo "  make armhf     - Build for 32-bit ARM (hard-float)"
	@echo "  make x86       - Build for 32-bit x86"
	@echo "  make riscv64   - Build for 64-bit RISC-V"
	@echo ""
	@echo "Advanced targets:"
	@echo "  make all-build   - Build for all supported architectures"
	@echo "  make all-tarball - Build all and create unified distribution"
	@echo "  make module      - Build all and create the KernelSU module zip"
	@echo ""
	@echo "Options:"
	@echo "  V=1            - Show full compiler commands"
	@echo "  MUSL_CROSS=dir - Directory holding <triplet>-gcc"
	@echo "  NO_STRIP=1     - Keep symbols"
	@echo ""
	@echo "Other:"
	@echo "  make bump NEW=x.y.z - Set the version (bridge.h)"
	@echo "  make clean     - Remove build artifacts"
	@echo "  make debug-hardened - Build with ASan/UBSan/LSan to find bugs"
	@echo "  make format    - Run clang-format on all .c/.h/.cpp files"
	@echo "  make test      - Run the smoke test against output/$(BINARY_NAME) (root, Linux)"

# A real daemon on a temporary directory, so it needs root and /system/bin/sh.
# Build first with `make native`, or point it at another binary with BIN=.
BIN ?= $(OUT_DIR)/$(BINARY_NAME)
test:
	./tests/smoke.sh $(BIN)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(msg_cc)
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BINARY): $(OBJS)
	$(msg_ld)
	$(Q)$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
	@echo "[+] Built: $@"

# output/android-bridge is always the arch asked for last, built or not, for
# the device-test instructions
native:
	@if [ -n "$(NATIVE_CC)" ]; then $(MAKE) --no-print-directory -j$(NPROC) $(BINARY) CC="$(NATIVE_CC)" && cp -f $(BINARY) $(OUT_DIR)/$(BINARY_NAME); \
	else echo "Error: no musl toolchain for $(ARCH) ($(triplet_$(ARCH))-gcc). Run scripts/install-musl.sh $(ARCH) or set MUSL_CROSS"; exit 1; fi

triplet_x86_64  = x86_64-linux-musl
triplet_aarch64 = aarch64-linux-musl
triplet_x86     = i686-linux-musl
triplet_armhf   = arm-linux-musleabihf
triplet_riscv64 = riscv64-linux-musl

# A musl CC is used as is, anything else means the host arch's cross toolchain
NATIVE_CC := $(if $(findstring musl,$(shell $(CC) -dumpmachine 2>/dev/null)),$(CC),$(call find-cc,$(triplet_$(ARCH))))

# The sub-make re-derives ARCH, OBJ_DIR and BINARY from the cross compiler
$(ARCHES):
	@CROSS_CC="$(call find-cc,$(triplet_$@))"; \
	if [ -n "$$CROSS_CC" ]; then $(MAKE) --no-print-directory -j$(NPROC) $(OUT_DIR)/$(BINARY_NAME)-$@ CC=$$CROSS_CC && cp -f $(OUT_DIR)/$(BINARY_NAME)-$@ $(OUT_DIR)/$(BINARY_NAME); \
	else echo "Error: $(triplet_$@)-gcc not found. Run scripts/install-musl.sh $@ or set MUSL_CROSS"; exit 1; fi

debug-hardened:
	@echo "[*] Building hardened debug binary..."
	@mkdir -p $(OUT_DIR)
	$(Q)$(CC) $(SRCS) -o $(OUT_DIR)/$(BINARY_NAME)-hardened \
		-I$(SRC_DIR)/include -I$(SRC_DIR) -g3 -O1 -pthread -lutil \
		-fsanitize=address -fsanitize=undefined -fsanitize=leak \
		-fstack-protector-strong -D_FORTIFY_SOURCE=2 \
		-Wall -Wextra -Wno-unused-parameter
	@echo "[+] Hardened binary built: $(OUT_DIR)/$(BINARY_NAME)-hardened"
	@echo "[!] Note: Run this on a standard Linux host (not static/musl) for best results."

# Stops at the first arch that fails; an unchanged arch costs nothing
all-build:
	@for a in $(ARCHES); do \
		$(MAKE) --no-print-directory $$a || exit 1; \
	done
	@echo "[+] All architectures built successfully in $(OUT_DIR)/"

all-tarball: all-build
	@COMMIT_HASH=$$(git rev-parse --short HEAD 2>/dev/null || date +%Y-%m-%d); \
	TARBALL="$(BINARY_NAME)-v$(VERSION)-$$COMMIT_HASH.tar.gz"; \
	ROOT_DIR="$(BINARY_NAME)-v$(VERSION)"; \
	TEMP_DIR="/tmp/android-bridge-tarball-$$$$"; \
	mkdir -p $$TEMP_DIR/$$ROOT_DIR; \
	for arch in $(ARCHES); do \
		mkdir -p $$TEMP_DIR/$$ROOT_DIR/$$arch; \
		cp $(OUT_DIR)/$(BINARY_NAME)-$$arch $$TEMP_DIR/$$ROOT_DIR/$$arch/$(BINARY_NAME); \
	done; \
	echo "[*] Creating unified distribution: $$TARBALL..."; \
	tar -czf $$TARBALL -C $$TEMP_DIR $$ROOT_DIR; \
	rm -rf $$TEMP_DIR; \
	echo "[+] Created: $$TARBALL ($$(du -h $$TARBALL | cut -f1))"

# Every arch goes in, and customize.sh keeps the one KernelSU reports
module: all-build
	@VERSION_CODE=$$(echo $(VERSION) | awk -F. '{print $$1*10000+$$2*100+$$3}'); \
	ZIP="$(BINARY_NAME)-module-v$(VERSION).zip"; \
	TEMP_DIR="/tmp/android-bridge-module-$$$$"; \
	mkdir -p $$TEMP_DIR; \
	cp module/customize.sh module/post-fs-data.sh module/sepolicy.rule $$TEMP_DIR/; \
	sed "s/@VERSION@/$(VERSION)/; s/@VERSIONCODE@/$$VERSION_CODE/" module/module.prop > $$TEMP_DIR/module.prop; \
	for arch in $(ARCHES); do \
		mkdir -p $$TEMP_DIR/bin/$$arch; \
		cp $(OUT_DIR)/$(BINARY_NAME)-$$arch $$TEMP_DIR/bin/$$arch/$(BINARY_NAME); \
	done; \
	echo "[*] Creating KernelSU module: $$ZIP..."; \
	rm -f $$ZIP; (cd $$TEMP_DIR && zip -qr9 - .) > $$ZIP; \
	rm -rf $$TEMP_DIR; \
	echo "[+] Created: $$ZIP ($$(du -h $$ZIP | cut -f1))"

bump:
	@echo "$(NEW)" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$$' || { echo "Usage: make bump NEW=x.y.z"; exit 1; }
	@sed -i 's/define AB_VERSION ".*"/define AB_VERSION "$(NEW)"/' $(SRC_DIR)/include/bridge.h
	@echo "[+] Version $(VERSION) -> $(NEW). Commit it, then tag v$(NEW) to release."

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "Error: clang-format not found"; exit 1; }
	@find $(SRC_DIR) -name '*.c' -o -name '*.h' -o -name '*.cpp' | xargs clang-format -i
	@echo "[+] Formatted all source files"

clean:
	@rm -rf $(OUT_DIR) $(BINARY_NAME)-*.tar.gz $(BINARY_NAME)-module-*.zip
	@echo "[+] Cleaned build artifacts"
