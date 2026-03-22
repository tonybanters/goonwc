CC = gcc
CFLAGS = -std=c23 -Wall -Wextra -Wno-unused-parameter -D_POSIX_C_SOURCE=200809L
CFLAGS += -I./include -I./lib/owl/include -I./lib/owl/src -I./build
LDFLAGS = $(shell pkg-config --libs wayland-server xkbcommon libdrm gbm egl glesv2 libinput libudev)
CFLAGS += $(shell pkg-config --cflags wayland-server xkbcommon libdrm gbm egl glesv2 libinput libudev)

DWC_SRC = src/main.c src/server.c
OWL_SRC = lib/owl/src/owl.c \
          lib/owl/src/render.c \
          lib/owl/src/input.c

PROTOCOL_XML = lib/owl/protocols/xdg-shell.xml
PROTOCOL_H = build/xdg-shell-protocol.h
PROTOCOL_C = build/xdg-shell-protocol.c

LAYER_SHELL_XML = lib/owl/protocols/wlr-layer-shell-unstable-v1.xml
LAYER_SHELL_H = build/wlr-layer-shell-unstable-v1-protocol.h
LAYER_SHELL_C = build/wlr-layer-shell-unstable-v1-protocol.c

XDG_OUTPUT_XML = lib/owl/protocols/xdg-output-unstable-v1.xml
XDG_OUTPUT_H = build/xdg-output-unstable-v1-protocol.h
XDG_OUTPUT_C = build/xdg-output-unstable-v1-protocol.c

EXT_WORKSPACE_XML = lib/owl/protocols/ext-workspace-v1.xml
EXT_WORKSPACE_H = build/ext-workspace-v1-protocol.h
EXT_WORKSPACE_C = build/ext-workspace-v1-protocol.c

XDG_DECORATION_XML = lib/owl/protocols/xdg-decoration-unstable-v1.xml
XDG_DECORATION_H = build/xdg-decoration-protocol.h
XDG_DECORATION_C = build/xdg-decoration-protocol.c

SCREENCOPY_XML = lib/owl/protocols/wlr-screencopy-unstable-v1.xml
SCREENCOPY_H = build/wlr-screencopy-unstable-v1-protocol.h
SCREENCOPY_C = build/wlr-screencopy-unstable-v1-protocol.c

PRIMARY_SEL_XML = lib/owl/protocols/wp-primary-selection-unstable-v1.xml
PRIMARY_SEL_H = build/wp-primary-selection-unstable-v1-protocol.h
PRIMARY_SEL_C = build/wp-primary-selection-unstable-v1-protocol.c

ALL_SRC = $(DWC_SRC) $(OWL_SRC)
OBJ = $(ALL_SRC:.c=.o)

TARGET = dwc

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(PROTOCOL_H) $(PROTOCOL_C) $(LAYER_SHELL_H) $(LAYER_SHELL_C) $(XDG_OUTPUT_H) $(XDG_OUTPUT_C) $(EXT_WORKSPACE_H) $(EXT_WORKSPACE_C) $(XDG_DECORATION_H) $(XDG_DECORATION_C) $(SCREENCOPY_H) $(SCREENCOPY_C) $(PRIMARY_SEL_H) $(PRIMARY_SEL_C) $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

build:
	mkdir -p build

$(PROTOCOL_H): $(PROTOCOL_XML) | build
	wayland-scanner server-header $< $@

$(PROTOCOL_C): $(PROTOCOL_XML) | build
	wayland-scanner private-code $< $@

$(LAYER_SHELL_H): $(LAYER_SHELL_XML) | build
	wayland-scanner server-header $< $@

$(LAYER_SHELL_C): $(LAYER_SHELL_XML) | build
	wayland-scanner private-code $< $@

$(XDG_OUTPUT_H): $(XDG_OUTPUT_XML) | build
	wayland-scanner server-header $< $@

$(XDG_OUTPUT_C): $(XDG_OUTPUT_XML) | build
	wayland-scanner private-code $< $@

$(EXT_WORKSPACE_H): $(EXT_WORKSPACE_XML) | build
	wayland-scanner server-header $< $@

$(EXT_WORKSPACE_C): $(EXT_WORKSPACE_XML) | build
	wayland-scanner private-code $< $@

$(XDG_DECORATION_H): $(XDG_DECORATION_XML) | build
	wayland-scanner server-header $< $@

$(XDG_DECORATION_C): $(XDG_DECORATION_XML) | build
	wayland-scanner private-code $< $@

$(SCREENCOPY_H): $(SCREENCOPY_XML) | build
	wayland-scanner server-header $< $@

$(SCREENCOPY_C): $(SCREENCOPY_XML) | build
	wayland-scanner private-code $< $@

$(PRIMARY_SEL_H): $(PRIMARY_SEL_XML) | build
	wayland-scanner server-header $< $@

$(PRIMARY_SEL_C): $(PRIMARY_SEL_XML) | build
	wayland-scanner private-code $< $@

%.o: %.c $(PROTOCOL_H) $(PROTOCOL_C) $(LAYER_SHELL_H) $(LAYER_SHELL_C) $(XDG_OUTPUT_H) $(XDG_OUTPUT_C) $(EXT_WORKSPACE_H) $(EXT_WORKSPACE_C) $(XDG_DECORATION_H) $(XDG_DECORATION_C) $(SCREENCOPY_H) $(SCREENCOPY_C) $(PRIMARY_SEL_H) $(PRIMARY_SEL_C)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)
	rm -rf build
