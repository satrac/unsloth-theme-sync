# SPDX-FileCopyrightText: 2026 unsloth-theme-sync contributors
# SPDX-License-Identifier: AGPL-3.0-only

PREFIX  ?= $(HOME)/.local
LIBDIR  ?= $(PREFIX)/lib
CC      ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS  ?= -O2 -Wall -Wextra
LDFLAGS ?=

PKGS    := gtk+-3.0 gio-2.0
MODULE  := unsloth-theme-module.so
SRC     := src/unsloth-theme-module.c

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS) 2>/dev/null)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS) 2>/dev/null)

.PHONY: all check-deps install uninstall clean

all: $(MODULE)

check-deps:
	@$(PKG_CONFIG) --exists $(PKGS) || { \
		echo "error: missing GTK3/GIO development files."; \
		echo "Install the GTK3 development package (e.g. libgtk-3-dev, gtk3-devel)."; \
		exit 1; \
	}

$(MODULE): $(SRC) | check-deps
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GTK_CFLAGS) -fPIC -shared \
		-o $@ $< $(LDFLAGS) $(GTK_LIBS)

install: $(MODULE)
	install -d "$(DESTDIR)$(LIBDIR)"
	install -m 0755 $(MODULE) "$(DESTDIR)$(LIBDIR)/$(MODULE)"

uninstall:
	rm -f "$(DESTDIR)$(LIBDIR)/$(MODULE)"

clean:
	rm -f $(MODULE)
