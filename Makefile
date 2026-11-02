# Droidspaces v7 - Build system
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

BINARY_NAME = droidspaces
SRC_DIR     = src
OUT_DIR     = output

# Default compiler. Do not allow recursive builds to accidentally get empty CC.
CC ?= cc

# DS_VERSION in droidspaces.h is the one place the version lives; `make bump` rewrites it
VERSION := $(shell grep "DS_VERSION" $(SRC_DIR)/include/droidspaces.h | awk '{print $$3}' | tr -d '"')

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
       $(SRC_DIR)/core/log.c \
       $(SRC_DIR)/core/string.c \
       $(SRC_DIR)/core/fs.c \
       $(SRC_DIR)/core/paths.c \
       $(SRC_DIR)/core/proc.c \
       $(SRC_DIR)/core/exec.c \
       $(SRC_DIR)/core/fdpass.c \
       $(SRC_DIR)/core/json.c \
       $(SRC_DIR)/security/peer_auth.c \
       $(SRC_DIR)/security/selinux.c \
       $(SRC_DIR)/config/config.c \
       $(SRC_DIR)/config/parse.c \
       $(SRC_DIR)/config/load.c \
       $(SRC_DIR)/config/save.c \
       $(SRC_DIR)/env/environment.c \
       $(SRC_DIR)/security/jail.c \
       $(SRC_DIR)/security/caps.c \
       $(SRC_DIR)/security/seccomp.c \
       $(SRC_DIR)/security/seccomp_shims.c \
       $(SRC_DIR)/rootfs/mount.c \
       $(SRC_DIR)/rootfs/image.c \
       $(SRC_DIR)/rootfs/volatile.c \
       $(SRC_DIR)/rootfs/binds.c \
       $(SRC_DIR)/dev/dev.c \
       $(SRC_DIR)/dev/devpts.c \
       $(SRC_DIR)/cgroup/discover.c \
       $(SRC_DIR)/cgroup/mount.c \
       $(SRC_DIR)/cgroup/v1.c \
       $(SRC_DIR)/cgroup/lifecycle.c \
       $(SRC_DIR)/cgroup/limits.c \
       $(SRC_DIR)/cgroup/stats.c \
       $(SRC_DIR)/virt/mem.c \
       $(SRC_DIR)/virt/cpu.c \
       $(SRC_DIR)/virt/affinity.c \
       $(SRC_DIR)/virt/vproc.c \
       $(SRC_DIR)/hw/gpu.c \
       $(SRC_DIR)/hw/hardware.c \
       $(SRC_DIR)/tty/pty.c \
       $(SRC_DIR)/tty/proxy.c \
       $(SRC_DIR)/tty/console.c \
       $(SRC_DIR)/net/nl/core.c \
       $(SRC_DIR)/net/nl/probe.c \
       $(SRC_DIR)/net/nl/link.c \
       $(SRC_DIR)/net/nl/addr.c \
       $(SRC_DIR)/net/nl/linkdump.c \
       $(SRC_DIR)/net/nl/route_query.c \
       $(SRC_DIR)/net/nl/rule.c \
       $(SRC_DIR)/net/ipt/core.c \
       $(SRC_DIR)/net/ipt/match.c \
       $(SRC_DIR)/net/ipt/insert.c \
       $(SRC_DIR)/net/ipt/remove.c \
       $(SRC_DIR)/net/ipt/rules.c \
       $(SRC_DIR)/net/ipt/portfwd.c \
       $(SRC_DIR)/net/dhcp/proto.c \
       $(SRC_DIR)/net/dhcp/server.c \
       $(SRC_DIR)/net/dhcp/ra.c \
       $(SRC_DIR)/net/naming.c \
       $(SRC_DIR)/net/netns.c \
       $(SRC_DIR)/net/dns.c \
       $(SRC_DIR)/net/static_ip.c \
       $(SRC_DIR)/net/nat/veth.c \
       $(SRC_DIR)/net/nat/filter.c \
       $(SRC_DIR)/net/route/policy.c \
       $(SRC_DIR)/net/route/uplink.c \
       $(SRC_DIR)/net/route/monitor.c \
       $(SRC_DIR)/net/gateway/wire.c \
       $(SRC_DIR)/net/gateway/lifecycle.c \
       $(SRC_DIR)/net/macvlan.c \
       $(SRC_DIR)/net/guest.c \
       $(SRC_DIR)/net/cleanup.c \
       $(SRC_DIR)/android/firmware.c \
       $(SRC_DIR)/android/spawn.c \
       $(SRC_DIR)/android/socket.c \
       $(SRC_DIR)/container/inspect.c \
       $(SRC_DIR)/container/registry.c \
       $(SRC_DIR)/container/recovery.c \
       $(SRC_DIR)/container/lock.c \
       $(SRC_DIR)/container/cleanup.c \
       $(SRC_DIR)/container/start.c \
       $(SRC_DIR)/container/stop.c \
       $(SRC_DIR)/container/exec.c \
       $(SRC_DIR)/container/info.c \
       $(SRC_DIR)/container/restart.c \
       $(SRC_DIR)/android/helpers.c \
       $(SRC_DIR)/status/collect.c \
       $(SRC_DIR)/status/show.c \
       $(SRC_DIR)/cli/options.c \
       $(SRC_DIR)/cli/usage.c \
       $(SRC_DIR)/android/android.c \
       $(SRC_DIR)/boot.c \
       $(SRC_DIR)/monitor.c \
       $(SRC_DIR)/documentation.c \
       $(SRC_DIR)/daemon.c \
       $(SRC_DIR)/check.c \
       $(SRC_DIR)/android/x11.c \
       $(SRC_DIR)/android/virgl.c \
       $(SRC_DIR)/android/pulseaudio.c \
       $(SRC_DIR)/anland/anland.c \
       $(SRC_DIR)/anland/display_daemon.c

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

find-cc = $(shell \
	if [ -n "$(MUSL_CROSS)" ] && [ -f "$(MUSL_CROSS)/$(1)-gcc" ]; then \
		echo "$(MUSL_CROSS)/$(1)-gcc"; \
	elif [ -f "$(HOME)/toolchains/$(1)-cross/bin/$(1)-gcc" ]; then \
		echo "$(HOME)/toolchains/$(1)-cross/bin/$(1)-gcc"; \
	elif command -v $(1)-gcc >/dev/null 2>&1; then \
		echo "$(1)-gcc"; \
	fi)

.PHONY: all help clean native x86_64 aarch64 armhf x86 riscv64 all-build all-tarball debug-hardened format bump test

all: help

help:
	@echo "Droidspaces v$(VERSION) Build System"
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
	@echo ""
	@echo "Options:"
	@echo "  V=1            - Show full compiler commands"
	@echo "  MUSL_CROSS=dir - Directory holding <triplet>-gcc"
	@echo "  NO_STRIP=1     - Keep symbols"
	@echo ""
	@echo "Other:"
	@echo "  make bump NEW=x.y.z - Set the version (droidspaces.h)"
	@echo "  make clean     - Remove build artifacts"
	@echo "  make debug-hardened - Build with ASan/UBSan/LSan to find bugs"
	@echo "  make format    - Run clang-format on all .c/.h/.cpp files"
	@echo "  make test      - Drive the installed binary through the running daemon (tests/, no sudo)"

# Black-box tests against the installed binary. They need a running daemon and a
# user in the droidspaces group, so install the build first:
#   sudo cp output/droidspaces /usr/bin/droidspaces && sudo systemctl restart droidspaces
test:
	python3 -m unittest discover -s tests -v

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(msg_cc)
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BINARY): $(OBJS)
	$(msg_ld)
	$(Q)$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
	@echo "[+] Built: $@"

# output/droidspaces is always the arch asked for last, built or not, for the
# device-test instructions
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
x86_64 aarch64 x86 armhf riscv64:
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
	@for a in x86_64 aarch64 armhf x86 riscv64; do \
		$(MAKE) --no-print-directory $$a || exit 1; \
	done
	@echo "[+] All architectures built successfully in $(OUT_DIR)/"

all-tarball: all-build
	@COMMIT_HASH=$$(git rev-parse --short HEAD 2>/dev/null || date +%Y-%m-%d); \
	TARBALL="$(BINARY_NAME)-v$(VERSION)-$$COMMIT_HASH.tar.gz"; \
	ROOT_DIR="$(BINARY_NAME)-v$(VERSION)"; \
	TEMP_DIR="/tmp/droidspaces-tarball-$$$$"; \
	mkdir -p $$TEMP_DIR/$$ROOT_DIR; \
	for arch in x86_64 aarch64 armhf x86 riscv64; do \
		mkdir -p $$TEMP_DIR/$$ROOT_DIR/$$arch; \
		cp $(OUT_DIR)/$(BINARY_NAME)-$$arch $$TEMP_DIR/$$ROOT_DIR/$$arch/$(BINARY_NAME); \
	done; \
	echo "[*] Creating unified distribution: $$TARBALL..."; \
	tar -czf $$TARBALL -C $$TEMP_DIR $$ROOT_DIR; \
	rm -rf $$TEMP_DIR; \
	echo "[+] Created: $$TARBALL ($$(du -h $$TARBALL | cut -f1))"

bump:
	@echo "$(NEW)" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$$' || { echo "Usage: make bump NEW=x.y.z"; exit 1; }
	@sed -i 's/define DS_VERSION ".*"/define DS_VERSION "$(NEW)"/' $(SRC_DIR)/include/droidspaces.h
	@echo "[+] Version $(VERSION) -> $(NEW). Commit it, then tag v$(NEW) to release."

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "Error: clang-format not found"; exit 1; }
	@find $(SRC_DIR) -name '*.c' -o -name '*.h' -o -name '*.cpp' | xargs clang-format -i
	@echo "[+] Formatted all source files"

clean:
	@rm -rf $(OUT_DIR) $(BINARY_NAME)-*.tar.gz
	@echo "[+] Cleaned build artifacts"
