#ifndef DWC_TYPES_H
#define DWC_TYPES_H

#include <owl/owl.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

typedef enum {
	DWC_CURSOR_PASSTHROUGH,
	DWC_CURSOR_MOVE,
	DWC_CURSOR_RESIZE,
} Dwc_Cursor_Mode;

typedef struct Dwc_Toplevel Dwc_Toplevel;
typedef struct Dwc_Server Dwc_Server;

struct Dwc_Toplevel {
	Dwc_Server *server;
	Owl_Window *window;
	int pos_x;
	int pos_y;
	unsigned int tags;      
	bool is_floating;       
	bool is_fullscreen;
	Dwc_Toplevel *next;
	Dwc_Toplevel *prev;
};

struct Dwc_Server {
	Owl_Display *display;

	Dwc_Toplevel *toplevels;
	Dwc_Toplevel *focused;
	int toplevel_count;

	unsigned int tagset;    
	float mfact;            

	Dwc_Cursor_Mode cursor_mode;
	Dwc_Toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	int grab_width, grab_height;
	int grab_pos_x, grab_pos_y;
	uint32_t resize_edges;
};

#endif
