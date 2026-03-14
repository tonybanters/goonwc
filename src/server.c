#include "dwc.h"
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* global server pointer for keybinding functions */
dwc_server *g_server = NULL;

/**
 * is_visible() - returns true if toplevel passed in is visible to user, based
 * on bitwise math. (since toplevel can be technically in multiple tags, we can
 * use & operator to calculate if users tagset overlaps with any of the tag(s)
 * the toplevel is visible on). might remove mutliple tags as a feature later,
 * but its easy as a default, and good proof of concept for light window manager
 * on top of OWL
 *
 * @toplevel: toplevel from compositor
 */
static bool is_visible(dwc_toplevel *toplevel) {
	return toplevel->tags & toplevel->server->tagset;
}

/**
 * is_tiled() - returns true if toplevel should participate in tiling layout.
 * a tiled window is visible, not floating, and not fullscreen.
 * @toplevel: toplevel to check
 */
static bool is_tiled(dwc_toplevel *toplevel) {
	return is_visible(toplevel) && !toplevel->is_floating && !toplevel->is_fullscreen;
}

/**
 * anchored_to() - checks if a layer surface's anchor flags contain all bits of
 * a given edge. Used to detect if a surface spans a full edge (like a bar) 
 * @anchor: anchor from layer surface (panel, bar, dock, etc.)
 * @edge: OWL_ANCHOR_EDGE from owl_anchor enum
 *
 * Return: true if anchor spans the full edge, false otherwise
 */
static bool anchored_to(uint32_t anchor, uint32_t edge) {
	return (anchor & edge) == edge;
}

/**
 * get_usable_area() - calculate screen area available for tiling
 * @server: the dwc server
 *
 * starts with full output dimensions and subtracts exclusive zones
 * claimed by layer surfaces (panels, bars, docks). A top-anchored
 * bar with exclusive_zone=30 would reduce height by 30 and shift y down by 30.
 *
 * Return: rect with usable area for tiling
 */
static rect get_usable_area(dwc_server *server) {
	int output_count = 0;
	owl_output **outputs = owl_display_get_outputs(server->display, &output_count);

	if (output_count == 0 || !outputs) {
		return (rect){0, 0, 0, 0};
	}

    /* todo: update for multiple monitors... */
	rect area = {0, 0, outputs[0]->width, outputs[0]->height};

	int layer_count = 0;
	owl_layer_surface **layers = owl_display_get_layer_surfaces(server->display, &layer_count);

	for (int i = 0; i < layer_count; i++) {
		owl_layer_surface *layer_surface = layers[i];
		if (!layer_surface->mapped) continue;

		int exclusive_zone = layer_surface->exclusive_zone;
		if (exclusive_zone <= 0) continue;

		uint32_t anchor = layer_surface->anchor;
		int margin_top = layer_surface->margin_top;
		int margin_right = layer_surface->margin_right;
		int margin_bottom = layer_surface->margin_bottom;
		int margin_left = layer_surface->margin_left;

        /**
         * reduce area based on which edge the layer surface claims 
         * ex: waybar's height/position gets reduced from height and added to 
         * y-coord, so new windows will take correct shape given waybar's space
         */
		if (anchored_to(anchor, OWL_ANCHOR_TOP_EDGE)) {
			area.y += exclusive_zone + margin_top + margin_bottom;
			area.h -= exclusive_zone + margin_top + margin_bottom;
		} else if (anchored_to(anchor, OWL_ANCHOR_BOTTOM_EDGE)) {
			area.h -= exclusive_zone + margin_top + margin_bottom;
		} else if (anchored_to(anchor, OWL_ANCHOR_LEFT_EDGE)) {
			area.x += exclusive_zone + margin_left + margin_right;
			area.w -= exclusive_zone + margin_left + margin_right;
		} else if (anchored_to(anchor, OWL_ANCHOR_RIGHT_EDGE)) {
			area.w -= exclusive_zone + margin_left + margin_right;
		}
	}

	return area;
}

/**
 * count_tiled() - counts number of tiled windows. a tiled window is a toplevel
 * that is not floating, not fullscreen, and visible.
 * @server: dwc server containing the toplevel list
 *
 * Return: number of tiled windows on the current tagset
 */
static int count_tiled(dwc_server *server) {
	int n = 0;
	dwc_toplevel *t;
	for (t = server->toplevels; t; t = t->next) {
		if (is_tiled(t)) {
			n++;
		}
	}
	return n;
}

/**
 * arrange() - for now, this function just arranges the tiled windows in a dwm
 * style mfact way. tiling mode is the only layout currently, and i will ofc 
 * add layouts soon.
 * @server: dwc server
 *
 */
void arrange(dwc_server *server) {
    /* todo: implement actual layouts, and call arrange(dwc->layout?) */
	rect area = get_usable_area(server);
	if (area.w <= 0 || area.h <= 0) return;

    /* todo: remove global, get from config struct */
    int g = gap; 

	dwc_toplevel *t;

    /* move invisible windows way offscreen. */
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
			if (is_tiled(t)) {
				owl_window_set_tiled(t->window, true);
				owl_window_move(t->window, area.x + g, area.y + g);
				owl_window_resize(t->window, area.w - 2 * g, area.h - 2 * g);
				break;
			}
		}
		return;
	}

	/* available width after outer gaps (left, right) and inner gap */
	int available_w = area.w - 3 * g;
	int master_w = (int)(available_w * server->mfact);
	int stack_w = available_w - master_w;

	int i = 0;
	int tile_y = area.y + g;
	int stack_count = n - 1;

	for (t = server->toplevels; t; t = t->next) {
		if (!is_tiled(t)) continue;

		owl_window_set_tiled(t->window, true);

		if (i == 0) {
			/* master window: outer gap on left, top, bottom */
			int master_x = area.x + g;
			int master_y = area.y + g;
			int master_h = area.h - 2 * g;
			owl_window_move(t->window, master_x, master_y);
			owl_window_resize(t->window, master_w, master_h);
		} else {
			/* stack windows: inner gap from master, outer gap on right */
			int remaining = stack_count - (i - 1);
			int stack_h = (area.y + area.h - tile_y - g - (remaining - 1) * g) / remaining;
			int stack_x = area.x + g + master_w + g;
			owl_window_move(t->window, stack_x, tile_y);
			owl_window_resize(t->window, stack_w, stack_h);
			tile_y += stack_h + g;
		}
		i++;
	}
}

/* keybinding action functions */
void spawn(void *arg) {
	arg_cmd *a = arg;
	if (fork() == 0) {
		setsid();
		setenv("WAYLAND_DISPLAY", owl_display_get_socket(g_server->display), 1);
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

static void update_workspace_states(dwc_server *server) {
	for (int i = 0; i < 9; i++) {
		if (server->workspaces[i]) {
			uint32_t state = (server->tagset & (1 << i)) ? OWL_WORKSPACE_STATE_ACTIVE : 0;
			owl_workspace_set_state(server->workspaces[i], state);
		}
	}
	owl_workspace_commit(server->display);
}

void view(void *arg) {
	arg_tag *a = arg;
	if (a->tag == g_server->tagset) return;
	g_server->tagset = a->tag;
	update_workspace_states(g_server);
	arrange(g_server);
	dwc_toplevel *t;
	for (t = g_server->toplevels; t; t = t->next) {
		if (is_visible(t)) {
			toplevel_focus(t);
			break;
		}
	}
}

void tag(void *arg) {
	arg_tag *a = arg;
	if (!g_server->focused) return;
	g_server->focused->tags = a->tag;
	arrange(g_server);
	if (!is_visible(g_server->focused)) {
		dwc_toplevel *t;
		for (t = g_server->toplevels; t; t = t->next) {
			if (is_visible(t)) {
				toplevel_focus(t);
				break;
			}
		}
	}
}

void toggleview(void *arg) {
	arg_tag *a = arg;
	unsigned int newtagset = g_server->tagset ^ a->tag;
	if (newtagset) {
		g_server->tagset = newtagset;
		arrange(g_server);
	}
}

void toggletag(void *arg) {
	arg_tag *a = arg;
	if (!g_server->focused) return;
	unsigned int newtags = g_server->focused->tags ^ a->tag;
	if (newtags) {
		g_server->focused->tags = newtags;
		arrange(g_server);
	}
}

void focusstack(void *arg) {
	arg_int *a = arg;
	if (!g_server->focused) return;

	dwc_toplevel *t = g_server->focused;
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
		dwc_toplevel *last = NULL;
		for (t = g_server->toplevels; t; t = t->next) {
			if (is_visible(t)) last = t;
		}
		if (last) toplevel_focus(last);
	}
}

void setmfact(void *arg) {
	arg_int *a = arg;
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

	dwc_toplevel *master = NULL;
	dwc_toplevel *t;
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
static void on_window_create(owl_display *display, owl_window *window, void *data) {
	(void)display;
	dwc_server *server = data;
	dwc_toplevel *toplevel = toplevel_create(server, window);
	if (toplevel) {
		window->user_data = toplevel;
		toplevel_focus(toplevel);
		arrange(server);
	}
}

static void on_window_destroy(owl_display *display, owl_window *window, void *data) {
	(void)display;
	dwc_server *server = data;
	dwc_toplevel *toplevel = window->user_data;
	if (toplevel) {
		toplevel_destroy(toplevel);
		arrange(server);
	}
}

static void on_window_request_move(owl_display *display, owl_window *window, void *data) {
	(void)display;
	(void)data;
	dwc_toplevel *toplevel = window->user_data;
	if (toplevel) {
		if (!toplevel->is_floating) {
			toplevel->is_floating = true;
			arrange(toplevel->server);
		}
		begin_interactive(toplevel, DWC_CURSOR_MOVE, 0);
	}
}

static void on_window_request_resize(owl_display *display, owl_window *window, void *data) {
	(void)display;
	(void)data;
	dwc_toplevel *toplevel = window->user_data;
	if (toplevel) {
		if (!toplevel->is_floating) {
			toplevel->is_floating = true;
			arrange(toplevel->server);
		}
		begin_interactive(toplevel, DWC_CURSOR_RESIZE, 0);
	}
}

static bool on_key_press(owl_display *display, owl_input *input, void *data) {
	(void)display;
	dwc_server *server = data;
	uint32_t keysym = input->keysym;
	uint32_t modifiers = input->modifiers;
	return handle_keybinding(server, keysym, modifiers);
}

static bool on_pointer_motion(owl_display *display, owl_input *input, void *data) {
	(void)display;
	dwc_server *server = data;
	int x = input->pointer_x;
	int y = input->pointer_y;

	if (server->cursor_mode == DWC_CURSOR_MOVE) {
		process_cursor_move(server, x, y);
		return true;
	} else if (server->cursor_mode == DWC_CURSOR_RESIZE) {
		process_cursor_resize(server, x, y);
		return true;
	}
	return false;
}

static bool on_button_press(owl_display *display, owl_input *input, void *data) {
	(void)display;
	dwc_server *server = data;
	int x = input->pointer_x;
	int y = input->pointer_y;

	dwc_toplevel *toplevel = toplevel_at(server, x, y);
	if (toplevel) {
		toplevel_focus(toplevel);
	}
	return false; /* don't consume button events */
}

static bool on_button_release(owl_display *display, owl_input *input, void *data) {
	(void)display;
	(void)input;
	dwc_server *server = data;
	reset_cursor_mode(server);
	return false; /* don't consume button events */
}

static void on_workspace_activate(owl_display *display, owl_workspace *workspace, void *data) {
	(void)display;
	dwc_server *server = data;
	const char *name = workspace->name;
	if (name && name[0] >= '1' && name[0] <= '9') {
		int tag_index = name[0] - '1';
		arg_tag arg = { .tag = 1u << tag_index };
		g_server = server;
		view(&arg);
	}
}

static void on_workspace_deactivate(owl_display *display, owl_workspace *workspace, void *data) {
	(void)display;
	(void)workspace;
	(void)data;
}

static void on_layer_surface_map(owl_display *display, owl_layer_surface *surface, void *data) {
	(void)display;
	(void)surface;
	dwc_server *server = data;
	arrange(server);
}

static void on_layer_surface_unmap(owl_display *display, owl_layer_surface *surface, void *data) {
	(void)display;
	(void)surface;
	dwc_server *server = data;
	arrange(server);
}

bool server_init(dwc_server *server) {
	memset(server, 0, sizeof(dwc_server));

	server->display = owl_display_create();
	if (!server->display) {
		fprintf(stderr, "dwc: failed to create owl display\n");
		return false;
	}

	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_CREATE, on_window_create, server);
	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_DESTROY, on_window_destroy, server);
	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_REQUEST_MOVE, on_window_request_move, server);
	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_REQUEST_RESIZE, on_window_request_resize, server);

	owl_set_input_callback(server->display, OWL_INPUT_EVENT_KEY_PRESS, on_key_press, server);
	owl_set_input_callback(server->display, OWL_INPUT_EVENT_POINTER_MOTION, on_pointer_motion, server);
	owl_set_input_callback(server->display, OWL_INPUT_EVENT_BUTTON_PRESS, on_button_press, server);
	owl_set_input_callback(server->display, OWL_INPUT_EVENT_BUTTON_RELEASE, on_button_release, server);

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

void server_run(dwc_server *server, const char *startup_cmd) {
	if (startup_cmd) {
		if (fork() == 0) {
			setenv("WAYLAND_DISPLAY", owl_display_get_socket(server->display), 1);
			setenv("XDG_SESSION_TYPE", "wayland", 1);
			setenv("XDG_CURRENT_DESKTOP", "dwc", 1);
			setenv("TERM", "xterm-256color", 1);
			setenv("COLORTERM", "truecolor", 1);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			_exit(1);
		}
	}

	fprintf(stderr, "dwc: running on %s\n", owl_display_get_socket(server->display));
	owl_display_run(server->display);
}

void server_cleanup(dwc_server *server) {
	while (server->toplevels) {
		toplevel_destroy(server->toplevels);
	}
	owl_display_destroy(server->display);
}

dwc_toplevel *toplevel_create(dwc_server *server, owl_window *window) {
	dwc_toplevel *toplevel = calloc(1, sizeof(dwc_toplevel));
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

void toplevel_destroy(dwc_toplevel *toplevel) {
	if (!toplevel) {
		return;
	}

	dwc_server *server = toplevel->server;

	if (server->focused == toplevel) {
		server->focused = NULL;
		/* focus next visible window */
		dwc_toplevel *t;
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

void toplevel_focus(dwc_toplevel *toplevel) {
	if (!toplevel) {
		return;
	}

	dwc_server *server = toplevel->server;

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

dwc_toplevel *toplevel_at(dwc_server *server, int x, int y) {
	dwc_toplevel *toplevel;
	for (toplevel = server->toplevels; toplevel; toplevel = toplevel->next) {
		if (!is_visible(toplevel)) continue;

		int wx = toplevel->window->x;
		int wy = toplevel->window->y;
		int ww = toplevel->window->width;
		int wh = toplevel->window->height;

		if (x >= wx && x < wx + ww && y >= wy && y < wy + wh) {
			return toplevel;
		}
	}
	return NULL;
}

bool handle_keybinding(dwc_server *server, uint32_t keysym, uint32_t modifiers) {
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

void begin_interactive(dwc_toplevel *toplevel, dwc_cursor_mode mode, uint32_t edges) {
	dwc_server *server = toplevel->server;

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;
	server->resize_edges = edges;

	int px, py;
	owl_display_get_pointer(server->display, &px, &py);
	server->grab_x = px;
	server->grab_y = py;
	server->grab_pos_x = toplevel->window->x;
	server->grab_pos_y = toplevel->window->y;
	server->grab_width = toplevel->window->width;
	server->grab_height = toplevel->window->height;
}

void reset_cursor_mode(dwc_server *server) {
	server->cursor_mode = DWC_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

void process_cursor_move(dwc_server *server, int x, int y) {
	dwc_toplevel *toplevel = server->grabbed_toplevel;
	if (!toplevel) {
		return;
	}

	int new_x = server->grab_pos_x + (x - (int)server->grab_x);
	int new_y = server->grab_pos_y + (y - (int)server->grab_y);

	owl_window_move(toplevel->window, new_x, new_y);
}

void process_cursor_resize(dwc_server *server, int x, int y) {
	dwc_toplevel *toplevel = server->grabbed_toplevel;
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
