CC ?= cc

CFLAGS += -std=c11 -Wall -Wextra -Werror -O2 -Wunused-result
CFLAGS += -Igenerated

# Use pkg-config for Wayland/Vulkan flags if available
PKG_CFLAGS := $(shell pkg-config --cflags wayland-client vulkan 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs wayland-client vulkan 2>/dev/null)

# Wayland protocol generation
WAYLAND_PROTOCOLS := /usr/share/wayland-protocols
WAYLAND_SCANNER := wayland-scanner

XDG_XML := $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml
XDG_HEADER := generated/xdg-shell-client-protocol.h
XDG_SOURCE := generated/xdg-shell-protocol.c

all: bin/flint

generated:
	mkdir -p $@

$(XDG_HEADER): $(XDG_XML) | generated
	$(WAYLAND_SCANNER) client-header $< $@

$(XDG_SOURCE): $(XDG_XML) | generated
	$(WAYLAND_SCANNER) private-code $< $@

bin:
	mkdir -p $@

bin/flint: main.c src/vk.c src/wl.c src/flint.c \
           $(XDG_SOURCE) $(XDG_HEADER) | bin
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ \
	      main.c src/vk.c src/wl.c src/flint.c \
	      $(XDG_SOURCE) $(PKG_LIBS)

check: bin/flint
	./bin/flint

clean:
	rm -rf bin dist generated

.PHONY: all check clean