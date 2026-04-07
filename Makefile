# SPDX-License-Identifier: GPL-2.0
obj-m := telegram_mod.o

KDIR ?= /lib/modules/$(shell uname -r)/build
CC   := gcc
CFLAGS := -Wall -Wextra -O2 -std=gnu11

CLANG_FORMAT := clang-format
CLANG_TIDY   := clang-tidy

KERNEL_SRC := telegram_mod.c
USER_SRC   := telegram_daemon.c

.PHONY: all module daemon clean install remove format lint check

all: module daemon

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

daemon: $(USER_SRC)
	$(CC) $(CFLAGS) -o telegram_daemon $<

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f telegram_daemon

install: all
	sudo insmod telegram_mod.ko
	sudo ./telegram_daemon &
	@echo "Module and daemon started."

remove:
	sudo pkill telegram_daemon 2>/dev/null || true
	sudo rmmod telegram_mod 2>/dev/null || true
	@echo "Module and daemon removed."

format: format-kernel format-user

format-kernel:
	@echo "Formatting kernel code..."
	$(CLANG_FORMAT) -i $(KERNEL_SRC)

format-user:
	@echo "Formatting user-space code..."
	$(CLANG_FORMAT) -i $(USER_SRC)

lint:
	@echo "Linting user-space code (clang-tidy)..."
	@$(CLANG_TIDY) $(USER_SRC) -- -std=gnu11 -Wall -Wextra -I/usr/include 2>/dev/null || true
	@echo "Kernel code should be checked with: ./scripts/checkpatch.pl -f $(KERNEL_SRC)"

check: format lint
