#ifndef GOONWC_H
#define GOONWC_H

#include "types.h"

bool server_init(goonwc_server *server);
void server_run(goonwc_server *server, const char *startup_cmd);
void server_cleanup(goonwc_server *server);

goonwc_toplevel *toplevel_create(goonwc_server *server, owl_window *window);
void toplevel_destroy(goonwc_toplevel *toplevel);
void toplevel_focus(goonwc_toplevel *toplevel);
goonwc_toplevel *toplevel_at(goonwc_server *server, int x, int y);

void arrange(goonwc_server *server);

bool handle_keybinding(goonwc_server *server, uint32_t keysym, uint32_t modifiers);

void begin_interactive(goonwc_toplevel *toplevel, goonwc_cursor_mode mode, uint32_t edges);
void reset_cursor_mode(goonwc_server *server);
void process_cursor_move(goonwc_server *server, int x, int y);
void process_cursor_resize(goonwc_server *server, int x, int y);

extern goonwc_server *g_server;

#endif
