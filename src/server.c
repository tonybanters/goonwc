#include "dwc.h"
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* global server pointer for keybinding functions */
Dwc_Server *g_server = NULL;

static Dwc_Toplevel *toplevel_from_window(Dwc_Server *server, Owl_Window *window) {
	Dwc_Toplevel *toplevel;
	for (toplevel = server->toplevels; toplevel; toplevel = toplevel->next) {
		if (toplevel->window == window) {
			return toplevel;
		}
	}
	return NULL;
}

bool is_visible(Dwc_Toplevel *toplevel) {
	return toplevel->tags & toplevel->server->tagset;
}

static int count_tiled(Dwc_Server *server) {
	int n = 0;
	Dwc_Toplevel *t;
	for (t = server->toplevels; t; t = t->next) {
		if (is_visible(t) && !t->is_floating && !t->is_fullscreen) {
			n++;
		}
	}
	return n;
}

void arrange(Dwc_Server *server) {
	int output_count = 0;
	Owl_Output **outputs = owl_get_outputs(server->display, &output_count);
	if (output_count == 0 || !outputs) return;

	int ww = owl_output_get_width(outputs[0]);
	int wh = owl_output_get_height(outputs[0]);
	int oh = gap_outer, ov = gap_outer;
	int ih = gap_inner, iv = gap_inner;

	Dwc_Toplevel *t;
	for (t = server->toplevels; t; t = t->next) {
		if (!is_visible(t)) {
			owl_window_move(t->window, -9999, -9999);
		}
	}

	int n = count_tiled(server);
	if (n == 0) return;

	int mx = ov;
	int my = oh;
	int mw = ww - 2 * ov;
	int mh = wh - 2 * oh;

	int sx = mx;
	int sy = my;
	int sw = mw;
	int sh = mh;

	if (n > 1) {
		mw = (int)((ww - 2 * ov - iv) * server->mfact);
		sw = ww - 2 * ov - iv - mw;
		sx = mx + mw + iv;
		sh = wh - 2 * oh - ih * (n - 2);
	}

	int i = 0;
	for (t = server->toplevels; t; t = t->next) {
		if (!is_visible(t) || t->is_floating || t->is_fullscreen)
			continue;

		if (i == 0) {
			owl_window_move(t->window, mx, my);
			owl_window_resize(t->window, mw, mh);
		} else {
			int h = sh / (n - 1);
			int y = sy + (i - 1) * (h + ih);
			owl_window_move(t->window, sx, y);
			owl_window_resize(t->window, sw, h);
		}
		i++;
	}
}

/* keybinding action functions */
void spawn(void *arg) {
	Arg_Cmd *a = arg;
	if (fork() == 0) {
		setsid();
		execvp(a->cmd[0], (char *const *)a->cmd);
		_exit(1);
	}
}

void killclient(void *arg) {
	(void)arg;
	if (g_server->focused) {
		owl_window_close(g_server->focused->window);
	}
}

void quit(void *arg) {
	(void)arg;
	owl_display_terminate(g_server->display);
}

void view(void *arg) {
	Arg_Tag *a = arg;
	if (a->tag == g_server->tagset) return;
	g_server->tagset = a->tag;
	arrange(g_server);
	Dwc_Toplevel *t;
	for (t = g_server->toplevels; t; t = t->next) {
		if (is_visible(t)) {
			toplevel_focus(t);
			break;
		}
	}
}

void tag(void *arg) {
	Arg_Tag *a = arg;
	if (!g_server->focused) return;
	g_server->focused->tags = a->tag;
	arrange(g_server);
	if (!is_visible(g_server->focused)) {
		Dwc_Toplevel *t;
		for (t = g_server->toplevels; t; t = t->next) {
			if (is_visible(t)) {
				toplevel_focus(t);
				break;
			}
		}
	}
}

void toggleview(void *arg) {
	Arg_Tag *a = arg;
	unsigned int newtagset = g_server->tagset ^ a->tag;
	if (newtagset) {
		g_server->tagset = newtagset;
		arrange(g_server);
	}
}

void toggletag(void *arg) {
	Arg_Tag *a = arg;
	if (!g_server->focused) return;
	unsigned int newtags = g_server->focused->tags ^ a->tag;
	if (newtags) {
		g_server->focused->tags = newtags;
		arrange(g_server);
	}
}

void focusstack(void *arg) {
	Arg_Int *a = arg;
	if (!g_server->focused) return;

	Dwc_Toplevel *t = g_server->focused;
	if (a->i > 0) {
		for (t = t->next; t; t = t->next) {
			if (is_visible(t)) {
				toplevel_focus(t);
				return;
			}
		}
		for (t = g_server->toplevels; t; t = t->next) {
			if (is_visible(t)) {
				toplevel_focus(t);
				return;
			}
		}
	} else {
		for (t = t->prev; t; t = t->prev) {
			if (is_visible(t)) {
				toplevel_focus(t);
				return;
			}
		}
		Dwc_Toplevel *last = NULL;
		for (t = g_server->toplevels; t; t = t->next) {
			if (is_visible(t)) last = t;
		}
		if (last) toplevel_focus(last);
	}
}

void setmfact(void *arg) {
	Arg_Int *a = arg;
	float delta = (float)a->i / 100.0f;
	float newmfact = g_server->mfact + delta;
	if (newmfact < 0.1f) newmfact = 0.1f;
	if (newmfact > 0.9f) newmfact = 0.9f;
	g_server->mfact = newmfact;
	arrange(g_server);
}

void togglefloating(void *arg) {
	(void)arg;
	if (!g_server->focused) return;
	g_server->focused->is_floating = !g_server->focused->is_floating;
	arrange(g_server);
}

void zoom(void *arg) {
	(void)arg;
	if (!g_server->focused || g_server->focused->is_floating) return;

	Dwc_Toplevel *master = NULL;
	Dwc_Toplevel *t;
	for (t = g_server->toplevels; t; t = t->next) {
		if (is_visible(t) && !t->is_floating) {
			master = t;
			break;
		}
	}

	if (g_server->focused == master) {
		for (t = master->next; t; t = t->next) {
			if (is_visible(t) && !t->is_floating) {
				toplevel_focus(t);
				arrange(g_server);
				return;
			}
		}
	} else {
		toplevel_focus(g_server->focused);
		arrange(g_server);
	}
}

/* callbacks */
static void on_window_create(Owl_Display *display, Owl_Window *window, void *data) {
	(void)display;
	Dwc_Server *server = data;
	Dwc_Toplevel *toplevel = toplevel_create(server, window);
	if (toplevel) {
		toplevel_focus(toplevel);
		arrange(server);
	}
}

static void on_window_destroy(Owl_Display *display, Owl_Window *window, void *data) {
	(void)display;
	Dwc_Server *server = data;
	Dwc_Toplevel *toplevel = toplevel_from_window(server, window);
	if (toplevel) {
		toplevel_destroy(toplevel);
		arrange(server);
	}
}

static void on_window_request_move(Owl_Display *display, Owl_Window *window, void *data) {
	(void)display;
	Dwc_Server *server = data;
	Dwc_Toplevel *toplevel = toplevel_from_window(server, window);
	if (toplevel) {
		if (!toplevel->is_floating) {
			toplevel->is_floating = true;
			arrange(server);
		}
		begin_interactive(toplevel, DWC_CURSOR_MOVE, 0);
	}
}

static void on_window_request_resize(Owl_Display *display, Owl_Window *window, void *data) {
	(void)display;
	Dwc_Server *server = data;
	Dwc_Toplevel *toplevel = toplevel_from_window(server, window);
	if (toplevel) {
		if (!toplevel->is_floating) {
			toplevel->is_floating = true;
			arrange(server);
		}
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
	server->tagset = 1; /* start on tag 1 */
	server->mfact = mfact; /* from config.h */

	g_server = server;

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
	toplevel->tags = server->tagset; /* new windows get current tag */
	toplevel->is_floating = false;
	toplevel->is_fullscreen = false;

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
		/* focus next visible window */
		Dwc_Toplevel *t;
		for (t = server->toplevels; t; t = t->next) {
			if (t != toplevel && is_visible(t)) {
				server->focused = t;
				owl_window_focus(t->window);
				break;
			}
		}
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

	/* move to front of list (master position for tiling) */
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
		if (!is_visible(toplevel)) continue;

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
	(void)server;

	uint32_t relevant_mods = OWL_MOD_SHIFT | OWL_MOD_CTRL | OWL_MOD_ALT | OWL_MOD_SUPER;
	uint32_t cleaned_mods = modifiers & relevant_mods;

	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (keys[i].keysym == keysym && keys[i].mod == cleaned_mods) {
			keys[i].func(keys[i].arg);
			return true;
		}
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
