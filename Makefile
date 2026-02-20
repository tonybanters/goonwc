CC = gcc
CFLAGS = -std=c23 -Wall -Wextra -Wno-unused-parameter -D_POSIX_C_SOURCE=200809L
CFLAGS += -I./include -I./lib/owl/include -I./lib/owl/src -I./build
LDFLAGS = $(shell pkg-config --libs wayland-server xkbcommon libdrm gbm egl glesv2 libinput libudev)
CFLAGS += $(shell pkg-config --cflags wayland-server xkbcommon libdrm gbm egl glesv2 libinput libudev)

DWC_SRC = src/main.c src/server.c
OWL_SRC = lib/owl/src/callbacks.c \
          lib/owl/src/display.c \
          lib/owl/src/input.c \
          lib/owl/src/layer_shell.c \
          lib/owl/src/output.c \
          lib/owl/src/render.c \
          lib/owl/src/surface.c \
          lib/owl/src/xdg_shell.c

PROTOCOL_XML = lib/owl/protocols/xdg-shell.xml
PROTOCOL_H = build/xdg-shell-protocol.h
PROTOCOL_C = build/xdg-shell-protocol.c

ALL_SRC = $(DWC_SRC) $(OWL_SRC)
OBJ = $(ALL_SRC:.c=.o)

TARGET = dwc

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(PROTOCOL_H) $(PROTOCOL_C) $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

build:
	mkdir -p build

$(PROTOCOL_H): $(PROTOCOL_XML) | build
	wayland-scanner server-header $< $@

$(PROTOCOL_C): $(PROTOCOL_XML) | build
	wayland-scanner private-code $< $@

%.o: %.c $(PROTOCOL_H) $(PROTOCOL_C)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)
	rm -rf build
