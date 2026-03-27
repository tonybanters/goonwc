#ifndef DWC_TYPES_H
#define DWC_TYPES_H

#include <owl/owl.h>
#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#define DWC_GESTURE_HISTORY_SIZE 32
#define DWC_GESTURE_HISTORY_MS 150

typedef struct {
	double dx;
	uint64_t timestamp_ms;
} dwc_gesture_event;

typedef enum {
	DWC_CURSOR_PASSTHROUGH,
	DWC_CURSOR_MOVE,
	DWC_CURSOR_RESIZE,
} dwc_cursor_mode;

typedef struct {
	int x, y, w, h;
} rect;

typedef struct dwc_toplevel dwc_toplevel;
typedef struct dwc_floating dwc_floating;
typedef struct dwc_server dwc_server;

/* niri-style column width */
typedef enum {
	DWC_WIDTH_PROPORTION,   /* fraction of screen (scales with monitor) */
	DWC_WIDTH_FIXED,        /* exact pixels (stays constant) */
} dwc_width_type;

typedef struct {
	dwc_width_type type;
	float value;            /* proportion (0.0-1.0) or pixels */
} dwc_width;

/* default presets: 1/3, 1/2, 2/3 */
#define DWC_PRESET_COUNT 3
static const float dwc_width_presets[DWC_PRESET_COUNT] = { 1.0f/3.0f, 0.5f, 2.0f/3.0f };

#define DWC_FOCUS_STACK_SIZE 64

typedef struct {
	dwc_toplevel *toplevel;
	int scroll_offset;
} dwc_focus_entry;

/* tiled window in scroll strip */
struct dwc_toplevel {
	dwc_server *server;
	owl_window *window;
	unsigned int tags;
	bool is_fullscreen;
	dwc_width width;        /* niri-style width */
	int preset_index;       /* which preset we're on (-1 if custom) */
	dwc_toplevel *next;
	dwc_toplevel *prev;
};

/* floating window (separate from scroll strip) */
struct dwc_floating {
	dwc_server *server;
	owl_window *window;
	float pos_x;            /* 0.0-1.0 fraction of working area */
	float pos_y;            /* 0.0-1.0 fraction of working area */
	int width;
	int height;
	unsigned int tags;
	dwc_floating *next;
	dwc_floating *prev;
};

struct dwc_server {
	owl_display *display;

	/* tiled windows (scroll strip) */
	dwc_toplevel *toplevels;
	dwc_toplevel *focused_tiled;
	int toplevel_count;

	/* floating windows (overlay) */
	dwc_floating *floating;
	dwc_floating *focused_floating;
	int floating_count;

	/* which space has focus */
	bool floating_is_active;

	unsigned int tagset;
	int scroll_offset;
	bool gesture_active;
	int gesture_start_offset;
	dwc_toplevel *gesture_start_focused;
	double gesture_cumulative_dx;
	dwc_gesture_event gesture_history[DWC_GESTURE_HISTORY_SIZE];
	int gesture_history_len;

	owl_workspace *workspaces[9];

	dwc_cursor_mode cursor_mode;
	dwc_toplevel *grabbed_tiled;
	dwc_floating *grabbed_floating;
	double grab_x, grab_y;
	int grab_width, grab_height;
	int grab_pos_x, grab_pos_y;
	uint32_t resize_edges;

	dwc_focus_entry focus_stack[DWC_FOCUS_STACK_SIZE];
	int focus_stack_len;
};

#endif
