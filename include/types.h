#ifndef GOONWC_TYPES_H
#define GOONWC_TYPES_H

#include <owl/owl.h>
#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include "config.h"

#define GOONWC_GESTURE_HISTORY_SIZE 32
#define GOONWC_GESTURE_HISTORY_MS 150

typedef struct {
	double dx;
	uint64_t timestamp_ms;
} goonwc_gesture_event;

typedef enum {
	GOONWC_CURSOR_PASSTHROUGH,
	GOONWC_CURSOR_MOVE,
	GOONWC_CURSOR_RESIZE,
} goonwc_cursor_mode;

typedef struct {
	int x, y, w, h;
} rect;

typedef struct goonwc_toplevel goonwc_toplevel;
typedef struct goonwc_floating goonwc_floating;
typedef struct goonwc_server goonwc_server;

typedef enum {
	GOONWC_WIDTH_PROPORTION,
	GOONWC_WIDTH_FIXED,
} goonwc_width_type;

typedef struct {
	goonwc_width_type type;
	float value;
} goonwc_width;

#define GOONWC_PRESET_COUNT 3
static const float goonwc_width_presets[GOONWC_PRESET_COUNT] = { 1.0f/3.0f, 0.5f, 2.0f/3.0f };

#define GOONWC_FOCUS_STACK_SIZE 64

typedef struct {
	goonwc_toplevel *toplevel;
	int scroll_offset;
} goonwc_focus_entry;

typedef struct {
	bool active;
	double from;
	double to;
	double velocity;
	uint64_t start_time_ms;
} goonwc_scroll_anim;

struct goonwc_toplevel {
	goonwc_server *server;
	owl_window *window;
	unsigned int tags;
	bool is_fullscreen;
	goonwc_width width;
	int preset_index;
	goonwc_toplevel *next;
	goonwc_toplevel *prev;
};

struct goonwc_floating {
	goonwc_server *server;
	owl_window *window;
	float pos_x;
	float pos_y;
	int width;
	int height;
	unsigned int tags;
	goonwc_floating *next;
	goonwc_floating *prev;
};

struct goonwc_server {
	owl_display *display;

	goonwc_config config;
	char *config_path;
	int inotify_fd;
	int inotify_wd;
	struct wl_event_source *config_event_source;

	goonwc_toplevel *toplevels;
	goonwc_toplevel *focused_tiled;
	int toplevel_count;

	goonwc_floating *floating;
	goonwc_floating *focused_floating;
	int floating_count;

	bool floating_is_active;

	unsigned int tagset;
	int scroll_offset;
	bool gesture_active;
	int gesture_start_offset;
	goonwc_toplevel *gesture_start_focused;
	double gesture_cumulative_dx;
	goonwc_gesture_event gesture_history[GOONWC_GESTURE_HISTORY_SIZE];
	int gesture_history_len;

	goonwc_scroll_anim scroll_anim;
	struct wl_event_source *anim_timer;

	owl_workspace *workspaces[9];

	goonwc_cursor_mode cursor_mode;
	goonwc_toplevel *grabbed_tiled;
	goonwc_floating *grabbed_floating;
	double grab_x, grab_y;
	int grab_width, grab_height;
	int grab_pos_x, grab_pos_y;
	uint32_t resize_edges;

	goonwc_focus_entry focus_stack[GOONWC_FOCUS_STACK_SIZE];
	int focus_stack_len;
};

#endif
