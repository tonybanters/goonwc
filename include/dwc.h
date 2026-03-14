#ifndef DWC_H
#define DWC_H

#include "types.h"

/* server lifecycle */
bool server_init(dwc_server *server);
void server_run(dwc_server *server, const char *startup_cmd);
void server_cleanup(dwc_server *server);

/* toplevel management */
dwc_toplevel *toplevel_create(dwc_server *server, owl_window *window);
void toplevel_destroy(dwc_toplevel *toplevel);
void toplevel_focus(dwc_toplevel *toplevel);
dwc_toplevel *toplevel_at(dwc_server *server, int x, int y);

/* layout */
void arrange(dwc_server *server);

/* keybinding handler */
bool handle_keybinding(dwc_server *server, uint32_t keysym, uint32_t modifiers);

/* interactive move/resize */
void begin_interactive(dwc_toplevel *toplevel, dwc_cursor_mode mode, uint32_t edges);
void reset_cursor_mode(dwc_server *server);
void process_cursor_move(dwc_server *server, int x, int y);
void process_cursor_resize(dwc_server *server, int x, int y);

/* global server pointer for keybinding functions */
extern dwc_server *g_server;

#endif
