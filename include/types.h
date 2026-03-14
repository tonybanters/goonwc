#ifndef DWC_TYPES_H
#define DWC_TYPES_H

#include <owl/owl.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

typedef enum {
	DWC_CURSOR_PASSTHROUGH,
	DWC_CURSOR_MOVE,
	DWC_CURSOR_RESIZE,
} dwc_cursor_mode;

typedef struct {
	int x, y, w, h;
} rect;

typedef struct dwc_toplevel dwc_toplevel;
typedef struct dwc_server dwc_server;

struct dwc_toplevel {
	dwc_server *server;
	owl_window *window;
	int pos_x;
	int pos_y;
	unsigned int tags;      
	bool is_floating;       
	bool is_fullscreen;
	dwc_toplevel *next;
	dwc_toplevel *prev;
};

struct dwc_server {
	owl_display *display;

	dwc_toplevel *toplevels;
	dwc_toplevel *focused;
	int toplevel_count;

	unsigned int tagset;
	float mfact;

	owl_workspace *workspaces[9];

	dwc_cursor_mode cursor_mode;
	dwc_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	int grab_width, grab_height;
	int grab_pos_x, grab_pos_y;
	uint32_t resize_edges;
};

#endif
