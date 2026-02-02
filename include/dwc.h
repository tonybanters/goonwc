#ifndef DWC_H
#define DWC_H

#include "types.h"

bool server_init(Dwc_Server *server);
void server_run(Dwc_Server *server, const char *startup_cmd);
void server_cleanup(Dwc_Server *server);

Dwc_Toplevel *toplevel_create(Dwc_Server *server, Owl_Window *window);
void toplevel_destroy(Dwc_Toplevel *toplevel);
void toplevel_focus(Dwc_Toplevel *toplevel);
Dwc_Toplevel *toplevel_at(Dwc_Server *server, int x, int y);

bool handle_keybinding(Dwc_Server *server, uint32_t keysym, uint32_t modifiers);

void begin_interactive(Dwc_Toplevel *toplevel, Dwc_Cursor_Mode mode, uint32_t edges);
void reset_cursor_mode(Dwc_Server *server);
void process_cursor_move(Dwc_Server *server, int x, int y);
void process_cursor_resize(Dwc_Server *server, int x, int y);

#endif
