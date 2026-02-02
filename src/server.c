#include "dwc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static Dwc_Toplevel *toplevel_from_window(Dwc_Server *server, Owl_Window *window) {
	Dwc_Toplevel *toplevel;
	for (toplevel = server->toplevels; toplevel; toplevel = toplevel->next) {
		if (toplevel->window == window) {
			return toplevel;
		}
	}
	return NULL;
}

static void on_window_create(Owl_Display *display, Owl_Window *window, void *data) {
	(void)display;
	Dwc_Server *server = data;
	Dwc_Toplevel *toplevel = toplevel_create(server, window);
	if (toplevel) {
		toplevel_focus(toplevel);
	}
}

static void on_window_destroy(Owl_Display *display, Owl_Window *window, void *data) {
	(void)display;
	Dwc_Server *server = data;
	Dwc_Toplevel *toplevel = toplevel_from_window(server, window);
	if (toplevel) {
		toplevel_destroy(toplevel);
	}
}

static void on_window_request_move(Owl_Display *display, Owl_Window *window, void *data) {
	(void)display;
	Dwc_Server *server = data;
	Dwc_Toplevel *toplevel = toplevel_from_window(server, window);
	if (toplevel) {
		begin_interactive(toplevel, DWC_CURSOR_MOVE, 0);
	}
}

static void on_window_request_resize(Owl_Display *display, Owl_Window *window, void *data) {
	(void)display;
	Dwc_Server *server = data;
	Dwc_Toplevel *toplevel = toplevel_from_window(server, window);
	if (toplevel) {
		begin_interactive(toplevel, DWC_CURSOR_RESIZE, 0);
	}
}

static void on_key_press(Owl_Display *display, Owl_Input *input, void *data) {
	(void)display;
	Dwc_Server *server = data;
	uint32_t keysym = owl_input_get_keysym(input);
	uint32_t modifiers = owl_input_get_modifiers(input);
	handle_keybinding(server, keysym, modifiers);
}

static void on_pointer_motion(Owl_Display *display, Owl_Input *input, void *data) {
	(void)display;
	Dwc_Server *server = data;
	int x = owl_input_get_pointer_x(input);
	int y = owl_input_get_pointer_y(input);

	if (server->cursor_mode == DWC_CURSOR_MOVE) {
		process_cursor_move(server, x, y);
	} else if (server->cursor_mode == DWC_CURSOR_RESIZE) {
		process_cursor_resize(server, x, y);
	}
}

static void on_button_press(Owl_Display *display, Owl_Input *input, void *data) {
	(void)display;
	Dwc_Server *server = data;
	int x = owl_input_get_pointer_x(input);
	int y = owl_input_get_pointer_y(input);

	Dwc_Toplevel *toplevel = toplevel_at(server, x, y);
	if (toplevel) {
		toplevel_focus(toplevel);
	}
}

static void on_button_release(Owl_Display *display, Owl_Input *input, void *data) {
	(void)display;
	(void)input;
	Dwc_Server *server = data;
	reset_cursor_mode(server);
}

bool server_init(Dwc_Server *server) {
	memset(server, 0, sizeof(Dwc_Server));

	server->display = owl_display_create();
	if (!server->display) {
		fprintf(stderr, "dwc: failed to create owl display\n");
		return false;
	}

	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_CREATE, on_window_create, server);
	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_DESTROY, on_window_destroy, server);
	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_REQUEST_MOVE, on_window_request_move, server);
	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_REQUEST_RESIZE, on_window_request_resize, server);

	owl_set_input_callback(server->display, OWL_INPUT_KEY_PRESS, on_key_press, server);
	owl_set_input_callback(server->display, OWL_INPUT_POINTER_MOTION, on_pointer_motion, server);
	owl_set_input_callback(server->display, OWL_INPUT_BUTTON_PRESS, on_button_press, server);
	owl_set_input_callback(server->display, OWL_INPUT_BUTTON_RELEASE, on_button_release, server);

	server->cursor_mode = DWC_CURSOR_PASSTHROUGH;

	return true;
}

void server_run(Dwc_Server *server, const char *startup_cmd) {
	if (startup_cmd) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			_exit(1);
		}
	}

	fprintf(stderr, "dwc: running on %s\n", owl_display_get_socket_name(server->display));
	owl_display_run(server->display);
}

void server_cleanup(Dwc_Server *server) {
	while (server->toplevels) {
		toplevel_destroy(server->toplevels);
	}
	owl_display_destroy(server->display);
}

Dwc_Toplevel *toplevel_create(Dwc_Server *server, Owl_Window *window) {
	Dwc_Toplevel *toplevel = calloc(1, sizeof(Dwc_Toplevel));
	if (!toplevel) {
		return NULL;
	}

	toplevel->server = server;
	toplevel->window = window;
	toplevel->pos_x = 0;
	toplevel->pos_y = 0;

	toplevel->next = server->toplevels;
	if (server->toplevels) {
		server->toplevels->prev = toplevel;
	}
	server->toplevels = toplevel;
	server->toplevel_count++;

	return toplevel;
}

void toplevel_destroy(Dwc_Toplevel *toplevel) {
	if (!toplevel) {
		return;
	}

	Dwc_Server *server = toplevel->server;

	if (server->focused == toplevel) {
		server->focused = NULL;
	}
	if (server->grabbed_toplevel == toplevel) {
		reset_cursor_mode(server);
	}

	if (toplevel->prev) {
		toplevel->prev->next = toplevel->next;
	} else {
		server->toplevels = toplevel->next;
	}
	if (toplevel->next) {
		toplevel->next->prev = toplevel->prev;
	}

	server->toplevel_count--;
	free(toplevel);
}

void toplevel_focus(Dwc_Toplevel *toplevel) {
	if (!toplevel) {
		return;
	}

	Dwc_Server *server = toplevel->server;

	if (server->focused == toplevel) {
		return;
	}

	server->focused = toplevel;
	owl_window_focus(toplevel->window);

	if (toplevel->prev) {
		toplevel->prev->next = toplevel->next;
		if (toplevel->next) {
			toplevel->next->prev = toplevel->prev;
		}
		toplevel->prev = NULL;
		toplevel->next = server->toplevels;
		if (server->toplevels) {
			server->toplevels->prev = toplevel;
		}
		server->toplevels = toplevel;
	}
}

Dwc_Toplevel *toplevel_at(Dwc_Server *server, int x, int y) {
	Dwc_Toplevel *toplevel;
	for (toplevel = server->toplevels; toplevel; toplevel = toplevel->next) {
		int wx = owl_window_get_x(toplevel->window);
		int wy = owl_window_get_y(toplevel->window);
		int ww = owl_window_get_width(toplevel->window);
		int wh = owl_window_get_height(toplevel->window);

		if (x >= wx && x < wx + ww && y >= wy && y < wy + wh) {
			return toplevel;
		}
	}
	return NULL;
}

bool handle_keybinding(Dwc_Server *server, uint32_t keysym, uint32_t modifiers) {
	if (!(modifiers & OWL_MOD_ALT)) {
		return false;
	}

	switch (keysym) {
	case XKB_KEY_Escape:
		owl_display_terminate(server->display);
		return true;

	case XKB_KEY_Return:
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", "foot", NULL);
			_exit(1);
		}
		return true;

	case XKB_KEY_q:
		if (server->focused) {
			owl_window_close(server->focused->window);
		}
		return true;

	case XKB_KEY_F1:
		if (server->toplevel_count >= 2 && server->toplevels && server->toplevels->next) {
			Dwc_Toplevel *last = server->toplevels;
			while (last->next) {
				last = last->next;
			}
			toplevel_focus(last);
		}
		return true;
	}

	return false;
}

void begin_interactive(Dwc_Toplevel *toplevel, Dwc_Cursor_Mode mode, uint32_t edges) {
	Dwc_Server *server = toplevel->server;

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;
	server->resize_edges = edges;

	server->grab_x = owl_display_get_pointer_x(server->display);
	server->grab_y = owl_display_get_pointer_y(server->display);
	server->grab_pos_x = owl_window_get_x(toplevel->window);
	server->grab_pos_y = owl_window_get_y(toplevel->window);
	server->grab_width = owl_window_get_width(toplevel->window);
	server->grab_height = owl_window_get_height(toplevel->window);
}

void reset_cursor_mode(Dwc_Server *server) {
	server->cursor_mode = DWC_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

void process_cursor_move(Dwc_Server *server, int x, int y) {
	Dwc_Toplevel *toplevel = server->grabbed_toplevel;
	if (!toplevel) {
		return;
	}

	int new_x = server->grab_pos_x + (x - (int)server->grab_x);
	int new_y = server->grab_pos_y + (y - (int)server->grab_y);

	owl_window_move(toplevel->window, new_x, new_y);
}

void process_cursor_resize(Dwc_Server *server, int x, int y) {
	Dwc_Toplevel *toplevel = server->grabbed_toplevel;
	if (!toplevel) {
		return;
	}

	int dx = x - (int)server->grab_x;
	int dy = y - (int)server->grab_y;

	int new_width = server->grab_width + dx;
	int new_height = server->grab_height + dy;

	if (new_width < 100) new_width = 100;
	if (new_height < 100) new_height = 100;

	owl_window_resize(toplevel->window, new_width, new_height);
}
