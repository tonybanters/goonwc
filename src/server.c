#include "dwc.h"
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

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
 * a tiled window is visible and not fullscreen.
 * @toplevel: toplevel to check
 */
static bool is_tiled(dwc_toplevel *toplevel) {
	return is_visible(toplevel) && !toplevel->is_fullscreen;
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
 * get_win_width() - niri-style window width calculation
 * @area_w: usable screen width
 * @gap: gap size in pixels
 * @width: dwc_width (proportion or fixed)
 *
 * for proportion: (area_w - gap) * proportion - gap
 * for fixed: just the fixed value
 *
 * Return: calculated window width in pixels
 */
static int get_win_width(int area_w, int g, dwc_width width) {
	if (width.type == DWC_WIDTH_FIXED) {
		return (int) width.value;
	}
	/* proportion: scale to screen */
	return (int)((area_w - g) * width.value - g);
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
 * count_tiled() - counts number of tiled windows. a tiled window is 
 * a toplevel that is not floating, not fullscreen, and visible.
 * @server: dwc server containing the toplevel list
 *
 * Return: number of arranged windows on the current tagset
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
 * arrange() - scroll layout. windows arranged horizontally in a strip.
 * @server: dwc server
 * @adjust_scroll: if true, scroll to keep focused window visible
 */
void arrange_internal(dwc_server *server, bool adjust_scroll) {
	rect area = get_usable_area(server);
	if (area.w <= 0 || area.h <= 0) return;

	int g = gap;
	dwc_toplevel *t;

	/* move invisible windows offscreen */
	for (t = server->toplevels; t; t = t->next) {
		if (!is_visible(t)) {
			owl_window_move(t->window, -9999, -9999);
		}
	}

	/* handle fullscreen window - covers everything */
	for (t = server->toplevels; t; t = t->next) {
		if (is_visible(t) && t->is_fullscreen) {
			owl_window_move(t->window, area.x, area.y);
			owl_window_resize(t->window, area.w, area.h);
			return;
		}
	}

	int n = count_tiled(server);
	if (n == 0) return;

	int win_height = area.h - 2 * g;

	/* single window: smart gaps (no gaps), fill usable area */
	if (n == 1) {
		server->scroll_offset = 0;
		for (t = server->toplevels; t; t = t->next) {
			if (!is_tiled(t)) continue;
			owl_window_set_tiled(t->window, true);
			owl_window_move(t->window, area.x, area.y);
            /* todo: for smart gaps, should we always fullscreen 1 window? */
			owl_window_resize(t->window, area.w, area.h);
			return;
		}
	}

	/*
	 * calculate strip layout: each window placed with gap before it
	 * strip: [g][win1][g][win2][g][win3][g]
	 * positions are relative to strip start (0)
	 */
	int focused_left = 0;
	int focused_right = 0;
	int pos = g;  /* start after left gap */
	bool found_focused = false;

	for (t = server->toplevels; t; t = t->next) {
		if (!is_tiled(t)) continue;

		int win_width = get_win_width(area.w, g, t->width);
		if (t == server->focused_tiled) {
			focused_left = pos;
			focused_right = pos + win_width;
			found_focused = true;
		}
		pos += win_width + g;
	}

	/* if focused isn't tiled, use first tiled */
	if (!found_focused) {
		pos = g;
		for (t = server->toplevels; t; t = t->next) {
			if (!is_tiled(t)) continue;
            int win_width = get_win_width(area.w, g, t->width);
			focused_left = pos;
			focused_right = pos + win_width;
			break;
		}
	}

	if (adjust_scroll && !server->gesture_active) {
		int view_left = server->scroll_offset;
		int view_right = server->scroll_offset + area.w;

		if (focused_left < view_left + g) {
			server->scroll_offset = focused_left - g;
		} else if (focused_right > view_right - g) {
			server->scroll_offset = focused_right - area.w + g;
		}

		if (server->scroll_offset < 0) {
			server->scroll_offset = 0;
		}
	}

	/* position all tiled windows */
	pos = area.x + g - server->scroll_offset;
	for (t = server->toplevels; t; t = t->next) {
		if (!is_tiled(t)) continue;

        int win_width = get_win_width(area.w, g, t->width);
		owl_window_set_tiled(t->window, true);
		owl_window_move(t->window, pos, area.y + g);
		owl_window_resize(t->window, win_width, win_height);
		pos += win_width + g;
	}
}

void arrange(dwc_server *server) {
	arrange_internal(server, true);
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
	if (g_server->focused_tiled) {
		owl_window_close(g_server->focused_tiled->window);
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
	if (!g_server->focused_tiled) return;
	g_server->focused_tiled->tags = a->tag;
	arrange(g_server);
	if (!is_visible(g_server->focused_tiled)) {
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
	if (!g_server->focused_tiled) return;
	unsigned int newtags = g_server->focused_tiled->tags ^ a->tag;
	if (newtags) {
		g_server->focused_tiled->tags = newtags;
		arrange(g_server);
	}
}

void focusstack(void *arg) {
	arg_int *a = arg;
	if (!g_server->focused_tiled) return;

	dwc_toplevel *t = g_server->focused_tiled;
	dwc_toplevel *target = NULL;

	if (a->i > 0) {
		/* focus next */
		for (t = t->next; t; t = t->next) {
			if (is_visible(t)) {
				target = t;
				break;
			}
		}
		/* wrap around to start */
		if (!target) {
			for (t = g_server->toplevels; t; t = t->next) {
				if (is_visible(t)) {
					target = t;
					break;
				}
			}
		}
	} else {
		/* focus prev */
		for (t = t->prev; t; t = t->prev) {
			if (is_visible(t)) {
				target = t;
				break;
			}
		}
		/* wrap around to end */
		if (!target) {
			for (t = g_server->toplevels; t; t = t->next) {
				if (is_visible(t)) target = t;
			}
		}
	}

	if (target && target != g_server->focused_tiled) {
		toplevel_focus(target);
		arrange(g_server);
	}
}

/**
 * toggle_width() - cycle through width presets (niri-style)
 * presets: 1/3, 1/2, 2/3 (wraps around)
 */
void toggle_width(void *arg) {
	arg_int *a = arg;
	if (!g_server->focused_tiled) return;
	dwc_toplevel *t = g_server->focused_tiled;

	int dir = (a && a->i < 0) ? -1 : 1;
	t->preset_index += dir;

	/* wrap around */
	if (t->preset_index >= DWC_PRESET_COUNT) t->preset_index = 0;
	if (t->preset_index < 0) t->preset_index = DWC_PRESET_COUNT - 1;

	t->width.type = DWC_WIDTH_PROPORTION;
	t->width.value = dwc_width_presets[t->preset_index];
	arrange(g_server);
}

/**
 * maximize() - set width to full screen (1.0)
 */
void maximize(void *arg) {
	(void)arg;
	if (!g_server->focused_tiled) return;
	dwc_toplevel *t = g_server->focused_tiled;

	/* toggle between full width and default preset */
	if (t->width.type == DWC_WIDTH_PROPORTION && t->width.value >= 0.99f) {
		/* restore to 1/2 (middle preset) */
		t->preset_index = 1;
		t->width.value = dwc_width_presets[1];
	} else {
		/* set to full */
		t->preset_index = -1; /* not a preset */
		t->width.type = DWC_WIDTH_PROPORTION;
		t->width.value = 1.0f;
	}
	arrange(g_server);
}

/**
 * togglefloating() - move window between tiled and floating space
 * TODO: implement once floating space is fully set up
 */
void togglefloating(void *arg) {
	(void)arg;
	/* TODO: move window from toplevels to floating list or vice versa */
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
		arrange_internal(server, false);
	}
}

static void on_window_request_move(owl_display *display, owl_window *window, void *data) {
	(void)display;
	(void)data;
	dwc_toplevel *toplevel = window->user_data;
	if (toplevel) {
		/* TODO: convert to floating and move */
		/* for now, tiled windows can't be moved via drag */
	}
}

static void on_window_request_resize(owl_display *display, owl_window *window, void *data) {
	(void)display;
	(void)data;
	dwc_toplevel *toplevel = window->user_data;
	if (toplevel) {
		/* TODO: convert to floating and resize, or resize tiled width */
		/* for now, tiled windows resize via keyboard only */
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

static void on_render_window(owl_display *display, owl_window *window, void *data) {
	(void)display;
	dwc_server *server = data;
	dwc_toplevel *toplevel = window->user_data;

	if (!toplevel) return;

	int x = window->x;
	int y = window->y;
	int w = window->width;
	int h = window->height;
	int bw = border_width;

	const float *color = (toplevel == server->focused_tiled) ? border_focused : border_unfocused;

	/* top */
	owl_render_rect(x - bw, y - bw, w + 2 * bw, bw, color[0], color[1], color[2], color[3]);
	/* bottom */
	owl_render_rect(x - bw, y + h, w + 2 * bw, bw, color[0], color[1], color[2], color[3]);
	/* left */
	owl_render_rect(x - bw, y, bw, h, color[0], color[1], color[2], color[3]);
	/* right */
	owl_render_rect(x + w, y, bw, h, color[0], color[1], color[2], color[3]);
}

static void on_gesture_begin(owl_display *display, owl_gesture *gesture, void *data) {
	(void)display;
	dwc_server *server = data;

	if (gesture->fingers == 3) {
		server->gesture_active = true;
	}
}

static void on_gesture_update(owl_display *display, owl_gesture *gesture, void *data) {
	(void)display;
	dwc_server *server = data;

	/* 3-finger horizontal swipe scrolls the strip */
	if (gesture->fingers == 3) {
		rect area = get_usable_area(server);
		int g = gap;

		/* calculate total strip width */
		int total_width = g;
		for (dwc_toplevel *t = server->toplevels; t; t = t->next) {
			if (is_tiled(t)) {
				total_width += get_win_width(area.w, g, t->width) + g;
			}
		}

		/* niri-style: positive dx = scroll view right */
		server->scroll_offset += (int)(gesture->dx * 2.0);

		/* niri-style bounds: allow overscroll by full screen width */
		int min_scroll = -area.w;
		int max_scroll = total_width;
		if (server->scroll_offset < min_scroll) server->scroll_offset = min_scroll;
		if (server->scroll_offset > max_scroll) server->scroll_offset = max_scroll;

		arrange(server);
	}
}

static void on_gesture_end(owl_display *display, owl_gesture *gesture, void *data) {
	(void)display;
	dwc_server *server = data;

	if (gesture->fingers == 3) {
		server->gesture_active = false;

		rect area = get_usable_area(server);
		int g = gap;
		int view_center = server->scroll_offset + area.w / 2;

		dwc_toplevel *best = NULL;
		int best_dist = INT_MAX;
		int pos = g;

		for (dwc_toplevel *t = server->toplevels; t; t = t->next) {
			if (!is_tiled(t)) continue;
			int win_width = get_win_width(area.w, g, t->width);
			int win_center = pos + win_width / 2;
			int dist = abs(win_center - view_center);
			if (dist < best_dist) {
				best_dist = dist;
				best = t;
			}
			pos += win_width + g;
		}

		if (best && best != server->focused_tiled) {
			server->focused_tiled = best;
			owl_window_focus(best->window);
		}

		arrange(server);
	}
}

bool server_init(dwc_server *server) {
	memset(server, 0, sizeof(dwc_server));

	server->display = owl_display_create();
	if (!server->display) {
		fprintf(stderr, "dwc: failed to create owl display\n");
		return false;
	}

	owl_set_keyboard_repeat(server->display, repeat_rate, repeat_delay);

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

	owl_set_gesture_callback(server->display, OWL_GESTURE_SWIPE_BEGIN, on_gesture_begin, server);
	owl_set_gesture_callback(server->display, OWL_GESTURE_SWIPE_UPDATE, on_gesture_update, server);
	owl_set_gesture_callback(server->display, OWL_GESTURE_SWIPE_END, on_gesture_end, server);

	owl_set_render_callback(server->display, on_render_window, server);

	server->cursor_mode = DWC_CURSOR_PASSTHROUGH;
	server->tagset = 1; /* start on tag 1 */

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

static int focus_stack_remove(dwc_server *server, dwc_toplevel *toplevel) {
	for (int i = 0; i < server->focus_stack_len; i++) {
		if (server->focus_stack[i].toplevel == toplevel) {
			int saved_scroll = server->focus_stack[i].scroll_offset;
			memmove(&server->focus_stack[i], &server->focus_stack[i + 1],
			        (server->focus_stack_len - i - 1) * sizeof(dwc_focus_entry));
			server->focus_stack_len--;
			return saved_scroll;
		}
	}
	return server->scroll_offset;
}

static void focus_stack_push(dwc_server *server, dwc_toplevel *toplevel) {
	focus_stack_remove(server, toplevel);

	if (server->focus_stack_len >= DWC_FOCUS_STACK_SIZE) {
		memmove(&server->focus_stack[0], &server->focus_stack[1],
		        (DWC_FOCUS_STACK_SIZE - 1) * sizeof(dwc_focus_entry));
		server->focus_stack_len = DWC_FOCUS_STACK_SIZE - 1;
	}

	server->focus_stack[server->focus_stack_len++] = (dwc_focus_entry){
		.toplevel = toplevel,
		.scroll_offset = server->scroll_offset,
	};
}

static dwc_focus_entry *focus_stack_get_previous(dwc_server *server) {
	for (int i = server->focus_stack_len - 1; i >= 0; i--) {
		dwc_focus_entry *entry = &server->focus_stack[i];
		if (entry->toplevel && is_visible(entry->toplevel)) {
			return entry;
		}
	}
	return NULL;
}

dwc_toplevel *toplevel_create(dwc_server *server, owl_window *window) {
	dwc_toplevel *toplevel = calloc(1, sizeof(dwc_toplevel));
	if (!toplevel) {
		return NULL;
	}

	toplevel->server = server;
	toplevel->window = window;
	toplevel->tags = server->tagset; /* new windows get current tag */
	toplevel->is_fullscreen = false;
	toplevel->preset_index = 1; /* start at 1/2 width */
	toplevel->width.type = DWC_WIDTH_PROPORTION;
	toplevel->width.value = dwc_width_presets[1]; /* 0.5 */

	/* insert after focused window (to the right in scroll layout) */
	if (server->focused_tiled) {
		toplevel->prev = server->focused_tiled;
		toplevel->next = server->focused_tiled->next;
		if (server->focused_tiled->next) {
			server->focused_tiled->next->prev = toplevel;
		}
		server->focused_tiled->next = toplevel;
	} else {
		/* no focused window, insert at head */
		toplevel->next = server->toplevels;
		if (server->toplevels) {
			server->toplevels->prev = toplevel;
		}
		server->toplevels = toplevel;
	}
	server->toplevel_count++;

	return toplevel;
}

void toplevel_destroy(dwc_toplevel *toplevel) {
	if (!toplevel) {
		return;
	}

	dwc_server *server = toplevel->server;

	int saved_scroll = focus_stack_remove(server, toplevel);

	if (server->focused_tiled == toplevel) {
		server->focused_tiled = NULL;
		dwc_focus_entry *entry = focus_stack_get_previous(server);
		if (entry) {
			server->focused_tiled = entry->toplevel;
			server->scroll_offset = saved_scroll;
			owl_window_focus(entry->toplevel->window);
		}
	}
	if (server->grabbed_tiled == toplevel) {
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

	if (server->focused_tiled == toplevel) {
		return;
	}

	server->focused_tiled = toplevel;
	focus_stack_push(server, toplevel);
	owl_window_focus(toplevel->window);
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

	server->grabbed_tiled = toplevel;
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
	server->grabbed_tiled = NULL;
}

void process_cursor_move(dwc_server *server, int x, int y) {
	dwc_toplevel *toplevel = server->grabbed_tiled;
	if (!toplevel) {
		return;
	}

	int new_x = server->grab_pos_x + (x - (int)server->grab_x);
	int new_y = server->grab_pos_y + (y - (int)server->grab_y);

	owl_window_move(toplevel->window, new_x, new_y);
}

void process_cursor_resize(dwc_server *server, int x, int y) {
	dwc_toplevel *toplevel = server->grabbed_tiled;
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
