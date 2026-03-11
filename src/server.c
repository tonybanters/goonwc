#include "dwc.h"
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* global server pointer for keybinding functions */
Dwc_Server *g_server = NULL;

/**
 * toplevel_from_window() - find a toplevel by its associated window
 * @server: the server containing the toplevel list
 * @window: the window to match against
 *
 * Return: the matching toplevel (or NULL if not found)
 */
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

/* calculate usable area after accounting for layer shell exclusive zones */
static void get_usable_area(Dwc_Server *server, int *x, int *y, int *w, int *h) {
	int output_count = 0;
	Owl_Output **outputs = owl_get_outputs(server->display, &output_count);
	if (output_count == 0 || !outputs) {
		*x = *y = 0;
		*w = *h = 0;
		return;
	}

	*x = 0;
	*y = 0;
	*w = owl_output_get_width(outputs[0]);
	*h = owl_output_get_height(outputs[0]);

	int layer_count = 0;
	Owl_Layer_Surface **layers = owl_get_layer_surfaces(server->display, &layer_count);

	for (int i = 0; i < layer_count; i++) {
		Owl_Layer_Surface *ls = layers[i];
		if (!owl_layer_surface_is_mapped(ls)) continue;

		int ez = owl_layer_surface_get_exclusive_zone(ls);
		if (ez <= 0) continue;

		uint32_t anchor = owl_layer_surface_get_anchor(ls);
		int margin_top = owl_layer_surface_get_margin_top(ls);
		int margin_bottom = owl_layer_surface_get_margin_bottom(ls);
		int margin_left = owl_layer_surface_get_margin_left(ls);
		int margin_right = owl_layer_surface_get_margin_right(ls);

		/* top anchored */
		if ((anchor & (OWL_ANCHOR_TOP | OWL_ANCHOR_LEFT | OWL_ANCHOR_RIGHT)) ==
		    (OWL_ANCHOR_TOP | OWL_ANCHOR_LEFT | OWL_ANCHOR_RIGHT)) {
			*y += ez + margin_top + margin_bottom;
			*h -= ez + margin_top + margin_bottom;
		}
		/* bottom anchored */
		else if ((anchor & (OWL_ANCHOR_BOTTOM | OWL_ANCHOR_LEFT | OWL_ANCHOR_RIGHT)) ==
		         (OWL_ANCHOR_BOTTOM | OWL_ANCHOR_LEFT | OWL_ANCHOR_RIGHT)) {
			*h -= ez + margin_top + margin_bottom;
		}
		/* left anchored */
		else if ((anchor & (OWL_ANCHOR_LEFT | OWL_ANCHOR_TOP | OWL_ANCHOR_BOTTOM)) ==
		         (OWL_ANCHOR_LEFT | OWL_ANCHOR_TOP | OWL_ANCHOR_BOTTOM)) {
			*x += ez + margin_left + margin_right;
			*w -= ez + margin_left + margin_right;
		}
		/* right anchored */
		else if ((anchor & (OWL_ANCHOR_RIGHT | OWL_ANCHOR_TOP | OWL_ANCHOR_BOTTOM)) ==
		         (OWL_ANCHOR_RIGHT | OWL_ANCHOR_TOP | OWL_ANCHOR_BOTTOM)) {
			*w -= ez + margin_left + margin_right;
		}
	}
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
	fprintf(stderr, "dwc: arrange called\n");
	fflush(stderr);
	int ax, ay, aw, ah;
	get_usable_area(server, &ax, &ay, &aw, &ah);
	fprintf(stderr, "dwc: usable area x=%d y=%d w=%d h=%d\n", ax, ay, aw, ah);
	fflush(stderr);
	if (aw <= 0 || ah <= 0) return;

	int g = gap;

	Dwc_Toplevel *t;
	for (t = server->toplevels; t; t = t->next) {
		if (!is_visible(t)) {
			owl_window_move(t->window, -9999, -9999);
		}
	}

	int n = count_tiled(server);
	if (n == 0) return;

	/* single window fills usable area minus gaps */
	if (n == 1) {
		for (t = server->toplevels; t; t = t->next) {
			if (is_visible(t) && !t->is_floating && !t->is_fullscreen) {
				owl_window_set_tiled(t->window, true);
				owl_window_move(t->window, ax + g, ay + g);
				owl_window_resize(t->window, aw - 2 * g, ah - 2 * g);
				break;
			}
		}
		return;
	}

	/* available width after outer gaps (left, right) and inner gap */
	int available_w = aw - 3 * g;
	int master_w = (int)(available_w * server->mfact);
	int stack_w = available_w - master_w;

	int i = 0;
	int ty = ay + g;
	int stack_count = n - 1;

	for (t = server->toplevels; t; t = t->next) {
		if (!is_visible(t) || t->is_floating || t->is_fullscreen)
			continue;

		owl_window_set_tiled(t->window, true);

		if (i == 0) {
			/* master window: outer gap on left, top, bottom */
			int mx = ax + g;
			int my = ay + g;
			int mheight = ah - 2 * g;
			fprintf(stderr, "dwc: master x=%d y=%d w=%d h=%d\n", mx, my, master_w, mheight);
			owl_window_move(t->window, mx, my);
			owl_window_resize(t->window, master_w, mheight);
		} else {
			/* stack windows: inner gap from master, outer gap on right */
			int remaining = stack_count - (i - 1);
			int h = (ay + ah - ty - g - (remaining - 1) * g) / remaining;
			int sx = ax + g + master_w + g;
			fprintf(stderr, "dwc: stack[%d] x=%d y=%d w=%d h=%d\n", i-1, sx, ty, stack_w, h);
			owl_window_move(t->window, sx, ty);
			owl_window_resize(t->window, stack_w, h);
			ty += h + g;
		}
		i++;
	}
}

/* keybinding action functions */
void spawn(void *arg) {
	Arg_Cmd *a = arg;
	if (fork() == 0) {
		setsid();
		setenv("WAYLAND_DISPLAY", owl_display_get_socket_name(g_server->display), 1);
		setenv("XDG_SESSION_TYPE", "wayland", 1);
		setenv("XDG_CURRENT_DESKTOP", "dwc", 1);
		setenv("TERM", "xterm-256color", 1);
		setenv("COLORTERM", "truecolor", 1);
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

static void update_workspace_states(Dwc_Server *server) {
	for (int i = 0; i < 9; i++) {
		if (server->workspaces[i]) {
			uint32_t state = (server->tagset & (1 << i)) ? OWL_WORKSPACE_STATE_ACTIVE : 0;
			owl_workspace_set_state(server->workspaces[i], state);
		}
	}
	owl_workspace_commit(server->display);
}

void view(void *arg) {
	Arg_Tag *a = arg;
	if (a->tag == g_server->tagset) return;
	g_server->tagset = a->tag;
	update_workspace_states(g_server);
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

static bool on_key_press(Owl_Display *display, Owl_Input *input, void *data) {
	(void)display;
	Dwc_Server *server = data;
	uint32_t keysym = owl_input_get_keysym(input);
	uint32_t modifiers = owl_input_get_modifiers(input);
	return handle_keybinding(server, keysym, modifiers);
}

static bool on_pointer_motion(Owl_Display *display, Owl_Input *input, void *data) {
	(void)display;
	Dwc_Server *server = data;
	int x = owl_input_get_pointer_x(input);
	int y = owl_input_get_pointer_y(input);

	if (server->cursor_mode == DWC_CURSOR_MOVE) {
		process_cursor_move(server, x, y);
		return true;
	} else if (server->cursor_mode == DWC_CURSOR_RESIZE) {
		process_cursor_resize(server, x, y);
		return true;
	}
	return false;
}

static bool on_button_press(Owl_Display *display, Owl_Input *input, void *data) {
	(void)display;
	Dwc_Server *server = data;
	int x = owl_input_get_pointer_x(input);
	int y = owl_input_get_pointer_y(input);

	Dwc_Toplevel *toplevel = toplevel_at(server, x, y);
	if (toplevel) {
		toplevel_focus(toplevel);
	}
	return false; /* don't consume button events */
}

static bool on_button_release(Owl_Display *display, Owl_Input *input, void *data) {
	(void)display;
	(void)input;
	Dwc_Server *server = data;
	reset_cursor_mode(server);
	return false; /* don't consume button events */
}

static void on_workspace_activate(Owl_Display *display, Owl_Workspace *workspace, void *data) {
	(void)display;
	Dwc_Server *server = data;
	const char *name = owl_workspace_get_name(workspace);
	if (name && name[0] >= '1' && name[0] <= '9') {
		int tag_index = name[0] - '1';
		Arg_Tag arg = { .tag = 1u << tag_index };
		g_server = server;
		view(&arg);
	}
}

static void on_workspace_deactivate(Owl_Display *display, Owl_Workspace *workspace, void *data) {
	(void)display;
	(void)workspace;
	(void)data;
}

static void on_layer_surface_map(Owl_Display *display, Owl_Layer_Surface *surface, void *data) {
	(void)display;
	(void)surface;
	Dwc_Server *server = data;
	arrange(server);
}

static void on_layer_surface_unmap(Owl_Display *display, Owl_Layer_Surface *surface, void *data) {
	(void)display;
	(void)surface;
	Dwc_Server *server = data;
	arrange(server);
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

	owl_set_workspace_callback(server->display, OWL_WORKSPACE_EVENT_ACTIVATE, on_workspace_activate, server);
	owl_set_workspace_callback(server->display, OWL_WORKSPACE_EVENT_DEACTIVATE, on_workspace_deactivate, server);

	owl_set_layer_surface_callback(server->display, OWL_LAYER_SURFACE_EVENT_MAP, on_layer_surface_map, server);
	owl_set_layer_surface_callback(server->display, OWL_LAYER_SURFACE_EVENT_UNMAP, on_layer_surface_unmap, server);

	server->cursor_mode = DWC_CURSOR_PASSTHROUGH;
	server->tagset = 1; /* start on tag 1 */
	server->mfact = mfact; /* from config.h */

	/* create 9 workspaces (1-9) */
	for (int i = 0; i < 9; i++) {
		char name[2] = { '1' + i, '\0' };
		server->workspaces[i] = owl_workspace_create(server->display, name);
	}
	/* tag 1 is active initially */
	if (server->workspaces[0]) {
		owl_workspace_set_state(server->workspaces[0], OWL_WORKSPACE_STATE_ACTIVE);
		owl_workspace_commit(server->display);
	}

	g_server = server;

	return true;
}

void server_run(Dwc_Server *server, const char *startup_cmd) {
	if (startup_cmd) {
		if (fork() == 0) {
			setenv("WAYLAND_DISPLAY", owl_display_get_socket_name(server->display), 1);
			setenv("XDG_SESSION_TYPE", "wayland", 1);
			setenv("XDG_CURRENT_DESKTOP", "dwc", 1);
			setenv("TERM", "xterm-256color", 1);
			setenv("COLORTERM", "truecolor", 1);
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
