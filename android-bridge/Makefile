# android-bridge build
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

NAME    := android-bridge
SRCS    := src/main.c src/proto.c src/daemon.c src/client.c
HDRS    := src/bridge.h
OUT     := out
VERSION := $(shell sed -n 's/^\#define AB_VERSION "\(.*\)"/\1/p' src/bridge.h)

CFLAGS  := -std=gnu99 -O2 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Werror \
           -Wshadow -Wformat=2 -Wcast-qual -fstack-protector-strong
LDFLAGS := -static -s

# The module carries one binary per Android ABI. riscv64 phones do not
# exist yet, add it here when they do.
ARCHES          := aarch64 armhf x86_64 x86
triplet_aarch64 := aarch64-linux-musl
triplet_armhf   := arm-linux-musleabihf
triplet_x86_64  := x86_64-linux-musl
triplet_x86     := i686-linux-musl

# Same lookup as the Droidspaces Makefile, so one toolchain install serves
# both, plus Debian's musl-gcc wrapper for host builds.
find-cc = $(shell \
	if [ -n "$(MUSL_CROSS)" ] && [ -f "$(MUSL_CROSS)/$(1)-gcc" ]; then \
		echo "$(MUSL_CROSS)/$(1)-gcc"; \
	elif [ -f "$(HOME)/toolchains/$(1)-cross/bin/$(1)-gcc" ]; then \
		echo "$(HOME)/toolchains/$(1)-cross/bin/$(1)-gcc"; \
	elif command -v $(1)-gcc >/dev/null 2>&1; then \
		echo "$(1)-gcc"; \
	fi)
HOST_CC := $(or $(shell command -v musl-gcc 2>/dev/null),$(CC))

.PHONY: native all module test format clean $(ARCHES)

native: $(OUT)/$(NAME)

$(OUT)/$(NAME): $(SRCS) $(HDRS)
	@mkdir -p $(OUT)
	$(HOST_CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

$(ARCHES): %: $(OUT)/$(NAME)-%

$(OUT)/$(NAME)-%: $(SRCS) $(HDRS)
	@mkdir -p $(OUT)
	@cc="$(call find-cc,$(triplet_$*))"; \
	if [ -z "$$cc" ]; then echo "Error: $(triplet_$*)-gcc not found. Run ../scripts/install-musl.sh $* or set MUSL_CROSS"; exit 1; fi; \
	echo "$$cc $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)"; \
	$$cc $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

all: $(ARCHES)

# KernelSU's installer hands customize.sh the ABI, which picks the binary.
module: all
	@rm -rf $(OUT)/module && mkdir -p $(OUT)/module
	cp module/customize.sh module/post-fs-data.sh module/sepolicy.rule $(OUT)/module/
	sed 's/@VERSION@/$(VERSION)/; s/@VERSIONCODE@/$(shell echo $(VERSION) | awk -F. '{print $$1*10000+$$2*100+$$3}')/' \
		module/module.prop > $(OUT)/module/module.prop
	@for a in $(ARCHES); do \
		mkdir -p $(OUT)/module/bin/$$a && cp $(OUT)/$(NAME)-$$a $(OUT)/module/bin/$$a/$(NAME); \
	done
	rm -f $(OUT)/$(NAME)-v$(VERSION).zip
	cd $(OUT)/module && zip -qr9 ../$(NAME)-v$(VERSION).zip .
	@echo "[+] Built: $(OUT)/$(NAME)-v$(VERSION).zip"

# Needs root, it runs a real daemon against a temporary directory.
test: native
	./tests/smoke.sh $(OUT)/$(NAME)

format:
	clang-format -i $(SRCS) $(HDRS)

clean:
	rm -rf $(OUT)
