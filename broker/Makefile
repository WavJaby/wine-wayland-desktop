# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 the wine-wayland-desktop contributors
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror

wine-dispatch: wine-dispatch.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f wine-dispatch

.PHONY: clean
