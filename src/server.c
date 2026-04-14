#include "goonwc.h"
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <math.h>
#include <wayland-server-core.h>

void arrange_output(goonwc_server *server, owl_output *output, bool adjust_scroll);
void arrange_all(goonwc_server *server);

static uint64_t get_time_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static goonwc_output_state *get_output_state(goonwc_server *server, owl_output *output) {
	for (int i = 0; i < server->output_count; i++) {
		if (server->output_states[i].output == output) {
			return &server->output_states[i];
		}
	}
	return NULL;
}

static goonwc_output_state *get_active_state(goonwc_server *server) {
	return get_output_state(server, server->active_output);
}

static goonwc_toplevel *get_focused_tiled(goonwc_server *server) {
	goonwc_output_state *state = get_active_state(server);
	return state ? state->focused : NULL;
}

#define SPRING_STIFFNESS 800.0
#define SPRING_DAMPING_RATIO 1.0
#define SPRING_EPSILON 0.5

static double spring_calc(goonwc_scroll_anim *anim, double t) {
	double stiffness = SPRING_STIFFNESS;
	double beta = SPRING_DAMPING_RATIO * sqrt(stiffness);
	double x0 = anim->from - anim->to;
	double v0 = anim->velocity;
	return anim->to + exp(-beta * t) * (x0 + (beta * x0 + v0) * t);
}

static int anim_tick(void *data);

static void anim_start(goonwc_output_state *state, goonwc_server *server, double from, double to, double velocity) {
	state->scroll_anim = (goonwc_scroll_anim){
		.active = true,
		.from = from,
		.to = to,
		.velocity = velocity,
		.start_time_ms = get_time_ms(),
	};
	if (!server->anim_timer) {
		struct wl_event_loop *loop = owl_display_get_event_loop(server->display);
		server->anim_timer = wl_event_loop_add_timer(loop, anim_tick, server);
	}
	wl_event_source_timer_update(server->anim_timer, 16);
}

static void anim_stop(goonwc_output_state *state, goonwc_server *server) {
	state->scroll_anim.active = false;
	(void)server;
}

static int anim_tick(void *data) {
	goonwc_server *server = data;
	bool any_active = false;

	for (int i = 0; i < server->output_count; i++) {
		goonwc_output_state *state = &server->output_states[i];
		if (!state->scroll_anim.active) continue;

		any_active = true;
		double t = (get_time_ms() - state->scroll_anim.start_time_ms) / 1000.0;
		double pos = spring_calc(&state->scroll_anim, t);
		double diff = fabs(pos - state->scroll_anim.to);

		if (diff < SPRING_EPSILON) {
			state->scroll_offset = (int)state->scroll_anim.to;
			anim_stop(state, server);
		} else {
			state->scroll_offset = (int)pos;
		}

		arrange_output(server, state->output, false);
	}

	if (any_active) {
		wl_event_source_timer_update(server->anim_timer, 16);
	} else if (server->anim_timer) {
		wl_event_source_timer_update(server->anim_timer, 0);
	}

	owl_display_request_frame(server->display);
	return 0;
}

/* global server pointer for keybinding functions */
goonwc_server *g_server = NULL;

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
static bool is_visible(goonwc_toplevel *toplevel) {
	return toplevel->tags & toplevel->server->tagset;
}

/**
 * is_tiled() - returns true if toplevel should participate in tiling layout.
 * a tiled window is visible and not fullscreen.
 * @toplevel: toplevel to check
 */
static bool is_tiled(goonwc_toplevel *toplevel) {
	return is_visible(toplevel) && !toplevel->is_fullscreen;
}

static bool is_tiled_on(goonwc_toplevel *toplevel, owl_output *output) {
	return is_tiled(toplevel) && toplevel->output == output;
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
 * @width: goonwc_width (proportion or fixed)
 *
 * for proportion: (area_w - gap) * proportion - gap
 * for fixed: just the fixed value
 *
 * Return: calculated window width in pixels
 */
static int get_win_width(int area_w, int g, goonwc_width width) {
	if (width.type == GOONWC_WIDTH_FIXED) {
		return (int) width.value;
	}
	/* proportion: scale to screen */
	return (int)((area_w - g) * width.value - g);
}

/**
 * get_usable_area() - calculate screen area available for tiling on an output
 * @server: the goonwc server
 * @output: the output to calculate usable area for
 *
 * starts with full output dimensions and subtracts exclusive zones
 * claimed by layer surfaces (panels, bars, docks). A top-anchored
 * bar with exclusive_zone=30 would reduce height by 30 and shift y down by 30.
 *
 * Return: rect with usable area for tiling
 */
static rect get_usable_area(goonwc_server *server, owl_output *output) {
	if (!output) {
		return (rect){0, 0, 0, 0};
	}

	rect area = {output->x, output->y, output->width, output->height};

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

static int count_tiled_on(goonwc_server *server, owl_output *output) {
	int n = 0;
	goonwc_toplevel *t;
	for (t = server->toplevels; t; t = t->next) {
		if (is_tiled_on(t, output)) {
			n++;
		}
	}
	return n;
}

void arrange_output(goonwc_server *server, owl_output *output, bool adjust_scroll) {
	if (!output) return;
	goonwc_output_state *state = get_output_state(server, output);
	if (!state) return;

	rect area = get_usable_area(server, output);
	if (area.w <= 0 || area.h <= 0) return;

	int g = server->config.gap;
	goonwc_toplevel *t;

	/* move windows not on this output offscreen (if not visible or wrong output) */
	for (t = server->toplevels; t; t = t->next) {
		if (t->output != output || !is_visible(t)) {
			if (t->output == output) {
				owl_window_move(t->window, -9999, -9999);
			}
		}
	}

	/* handle fullscreen window on this output */
	for (t = server->toplevels; t; t = t->next) {
		if (t->output == output && is_visible(t) && t->is_fullscreen) {
			owl_window_move(t->window, area.x, area.y);
			owl_window_resize(t->window, area.w, area.h);
			return;
		}
	}

	int n = count_tiled_on(server, output);
	if (n == 0) return;

	int win_height = area.h - 2 * g;

	if (n == 1) {
		state->scroll_offset = 0;
	}

	int focused_left = 0;
	int focused_right = 0;
	int pos = g;
	bool found_focused = false;

	for (t = server->toplevels; t; t = t->next) {
		if (!is_tiled_on(t, output)) continue;

		int win_width = get_win_width(area.w, g, t->width);
		if (t == state->focused) {
			focused_left = pos;
			focused_right = pos + win_width;
			found_focused = true;
		}
		pos += win_width + g;
	}

	if (!found_focused) {
		pos = g;
		for (t = server->toplevels; t; t = t->next) {
			if (!is_tiled_on(t, output)) continue;
			int win_width = get_win_width(area.w, g, t->width);
			focused_left = pos;
			focused_right = pos + win_width;
			break;
		}
	}

	if (adjust_scroll && !state->gesture_active) {
		int view_left = state->scroll_offset;
		int view_right = state->scroll_offset + area.w;

		if (focused_left < view_left + g) {
			state->scroll_offset = focused_left - g;
		} else if (focused_right > view_right - g) {
			state->scroll_offset = focused_right - area.w + g;
		}

		if (state->scroll_offset < 0) {
			state->scroll_offset = 0;
		}
	}

	pos = area.x + g - state->scroll_offset;
	for (t = server->toplevels; t; t = t->next) {
		if (!is_tiled_on(t, output)) continue;

		int win_width = get_win_width(area.w, g, t->width);
		owl_window_set_tiled(t->window, true);
		owl_window_move(t->window, pos, area.y + g);
		owl_window_resize(t->window, win_width, win_height);
		pos += win_width + g;
	}
}

void arrange_all(goonwc_server *server) {
	for (int i = 0; i < server->output_count; i++) {
		arrange_output(server, server->output_states[i].output, true);
	}
}

void arrange(goonwc_server *server) {
	arrange_all(server);
}

/* keybinding action functions */
static void spawn_cmd(goonwc_server *server, const char *cmd) {
	if (fork() == 0) {
		setsid();
		setenv("WAYLAND_DISPLAY", owl_display_get_socket(server->display), 1);
		setenv("XDG_SESSION_TYPE", "wayland", 1);
		setenv("XDG_CURRENT_DESKTOP", "goonwc", 1);
		setenv("TERM", "xterm-256color", 1);
		setenv("COLORTERM", "truecolor", 1);
		execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
		_exit(1);
	}
}

static void spawn_terminal(goonwc_server *server) {
	const char *term = server->config.terminal ? server->config.terminal : "foot";
	spawn_cmd(server, term);
}

static void killclient(goonwc_server *server) {
	goonwc_toplevel *focused = get_focused_tiled(server);
	if (focused) {
		owl_window_close(focused->window);
	}
}

static void update_workspace_states(goonwc_server *server) {
	for (int i = 0; i < 9; i++) {
		if (server->workspaces[i]) {
			uint32_t state = (server->tagset & (1 << i)) ? OWL_WORKSPACE_STATE_ACTIVE : 0;
			owl_workspace_set_state(server->workspaces[i], state);
		}
	}
	owl_workspace_commit(server->display);
}

static void view_tag(goonwc_server *server, int tag_idx) {
	unsigned int tag = 1 << tag_idx;
	if (tag == server->tagset) return;
	server->tagset = tag;
	update_workspace_states(server);
	arrange(server);
	for (goonwc_toplevel *t = server->toplevels; t; t = t->next) {
		if (is_visible(t)) {
			toplevel_focus(t);
			break;
		}
	}
}

static void move_to_tag(goonwc_server *server, int tag_idx) {
	goonwc_toplevel *focused = get_focused_tiled(server);
	if (!focused) return;
	focused->tags = 1 << tag_idx;
	arrange(server);
	if (!is_visible(focused)) {
		for (goonwc_toplevel *t = server->toplevels; t; t = t->next) {
			if (is_visible(t)) {
				toplevel_focus(t);
				break;
			}
		}
	}
}

static void toggle_view_tag(goonwc_server *server, int tag_idx) {
	unsigned int newtagset = server->tagset ^ (1 << tag_idx);
	if (newtagset) {
		server->tagset = newtagset;
		arrange(server);
	}
}

static void toggle_tag(goonwc_server *server, int tag_idx) {
	goonwc_toplevel *focused = get_focused_tiled(server);
	if (!focused) return;
	unsigned int newtags = focused->tags ^ (1 << tag_idx);
	if (newtags) {
		focused->tags = newtags;
		arrange(server);
	}
}

static void focus_next(goonwc_server *server) {
	goonwc_toplevel *focused = get_focused_tiled(server);
	if (!focused) return;
	goonwc_toplevel *target = NULL;
	for (goonwc_toplevel *t = focused->next; t; t = t->next) {
		if (is_tiled_on(t, focused->output)) { target = t; break; }
	}
	if (!target) {
		for (goonwc_toplevel *t = server->toplevels; t; t = t->next) {
			if (is_tiled_on(t, focused->output)) { target = t; break; }
		}
	}
	if (target && target != focused) {
		toplevel_focus(target);
		arrange(server);
	}
}

static void focus_prev(goonwc_server *server) {
	goonwc_toplevel *focused = get_focused_tiled(server);
	if (!focused) return;
	goonwc_toplevel *target = NULL;
	for (goonwc_toplevel *t = focused->prev; t; t = t->prev) {
		if (is_tiled_on(t, focused->output)) { target = t; break; }
	}
	if (!target) {
		for (goonwc_toplevel *t = server->toplevels; t; t = t->next) {
			if (is_tiled_on(t, focused->output)) target = t;
		}
	}
	if (target && target != focused) {
		toplevel_focus(target);
		arrange(server);
	}
}

static void toggle_width(goonwc_server *server, int dir) {
	goonwc_toplevel *t = get_focused_tiled(server);
	if (!t) return;

	t->preset_index += dir;
	if (t->preset_index >= GOONWC_PRESET_COUNT) t->preset_index = 0;
	if (t->preset_index < 0) t->preset_index = GOONWC_PRESET_COUNT - 1;

	t->width.type = GOONWC_WIDTH_PROPORTION;
	t->width.value = goonwc_width_presets[t->preset_index];
	arrange(server);
}

static void maximize(goonwc_server *server) {
	goonwc_toplevel *t = get_focused_tiled(server);
	if (!t) return;

	if (t->width.type == GOONWC_WIDTH_PROPORTION && t->width.value >= 0.99f) {
		t->preset_index = 1;
		t->width.value = goonwc_width_presets[1];
	} else {
		t->preset_index = -1;
		t->width.type = GOONWC_WIDTH_PROPORTION;
		t->width.value = 1.0f;
	}
	arrange(server);
}

static void togglefloating(goonwc_server *server) {
	(void)server;
}


/* callbacks */
static bool should_block_capture(goonwc_server *server, const char *app_id) {
	if (!app_id) return false;
	for (int i = 0; i < server->config.window_rule_count; i++) {
		goonwc_window_rule *rule = &server->config.window_rules[i];
		if (rule->block_screen_capture && rule->app_id && strcmp(app_id, rule->app_id) == 0)
			return true;
	}
	return false;
}

static void on_window_create(owl_display *display, owl_window *window, void *data) {
	(void)display;
	goonwc_server *server = data;
	goonwc_toplevel *toplevel = toplevel_create(server, window);
	if (toplevel) {
		window->user_data = toplevel;
		toplevel_focus(toplevel);
		arrange(server);
	}
}

static void on_window_map(owl_display *display, owl_window *window, void *data) {
	(void)display;
	goonwc_server *server = data;
	if (should_block_capture(server, window->app_id)) {
		owl_window_set_block_out_from(window, OWL_BLOCK_OUT_SCREEN_CAPTURE);
	}
}

static void on_window_destroy(owl_display *display, owl_window *window, void *data) {
	(void)display;
	goonwc_server *server = data;
	goonwc_toplevel *toplevel = window->user_data;
	if (toplevel) {
		toplevel_destroy(toplevel);
		arrange(server);
	}
}

static void on_window_request_move(owl_display *display, owl_window *window, void *data) {
	(void)display;
	(void)data;
	goonwc_toplevel *toplevel = window->user_data;
	if (toplevel) {
		/* TODO: convert to floating and move */
		/* for now, tiled windows can't be moved via drag */
	}
}

static void on_window_request_resize(owl_display *display, owl_window *window, void *data) {
	(void)display;
	(void)data;
	goonwc_toplevel *toplevel = window->user_data;
	if (toplevel) {
		/* TODO: convert to floating and resize, or resize tiled width */
		/* for now, tiled windows resize via keyboard only */
	}
}

static bool on_key_press(owl_display *display, owl_input *input, void *data) {
	(void)display;
	goonwc_server *server = data;
	uint32_t keysym = input->keysym;
	uint32_t modifiers = input->modifiers;
	return handle_keybinding(server, keysym, modifiers);
}

static bool on_pointer_motion(owl_display *display, owl_input *input, void *data) {
	(void)display;
	goonwc_server *server = data;
	int x = input->pointer_x;
	int y = input->pointer_y;

	if (server->cursor_mode == GOONWC_CURSOR_MOVE) {
		process_cursor_move(server, x, y);
		return true;
	} else if (server->cursor_mode == GOONWC_CURSOR_RESIZE) {
		process_cursor_resize(server, x, y);
		return true;
	}
	return false;
}

static bool on_button_press(owl_display *display, owl_input *input, void *data) {
	(void)display;
	goonwc_server *server = data;
	int x = input->pointer_x;
	int y = input->pointer_y;

	goonwc_toplevel *toplevel = toplevel_at(server, x, y);
	if (toplevel) {
		toplevel_focus(toplevel);
	}
	return false; /* don't consume button events */
}

static bool on_button_release(owl_display *display, owl_input *input, void *data) {
	(void)display;
	(void)input;
	goonwc_server *server = data;
	reset_cursor_mode(server);
	return false; /* don't consume button events */
}

static void on_workspace_activate(owl_display *display, owl_workspace *workspace, void *data) {
	(void)display;
	goonwc_server *server = data;
	const char *name = workspace->name;
	if (name && name[0] >= '1' && name[0] <= '9') {
		int tag_index = name[0] - '1';
		view_tag(server, tag_index);
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
	goonwc_server *server = data;
	arrange(server);
}

static void on_layer_surface_unmap(owl_display *display, owl_layer_surface *surface, void *data) {
	(void)display;
	(void)surface;
	goonwc_server *server = data;
	arrange(server);

	goonwc_toplevel *focused = get_focused_tiled(server);
	if (focused) {
		owl_window_focus(focused->window);
	}
}

static void on_render_window(owl_display *display, owl_window *window, void *data) {
	(void)display;
	goonwc_server *server = data;
	goonwc_toplevel *toplevel = window->user_data;

	if (!toplevel) return;

	int x = window->x;
	int y = window->y;
	int w = window->width;
	int h = window->height;
	int bw = server->config.border_width;

	goonwc_output_state *state = get_output_state(server, toplevel->output);
	bool is_focused = state && toplevel == state->focused;
	const float *color = is_focused ? server->config.border_focused : server->config.border_unfocused;

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
	goonwc_server *server = data;

	if (gesture->fingers == 3) {
		goonwc_output_state *state = get_active_state(server);
		if (!state) return;
		state->gesture_active = true;
		state->gesture_start_offset = state->scroll_offset;
		state->gesture_start_focused = state->focused;
		state->gesture_cumulative_dx = 0;
		state->gesture_history_len = 0;
	}
}

static void on_gesture_update(owl_display *display, owl_gesture *gesture, void *data) {
	(void)display;
	goonwc_server *server = data;

	if (gesture->fingers == 3) {
		goonwc_output_state *state = get_active_state(server);
		if (!state || !state->output) return;

		rect area = get_usable_area(server, state->output);
		int g = server->config.gap;

		int total_width = g;
		for (goonwc_toplevel *t = server->toplevels; t; t = t->next) {
			if (is_tiled_on(t, state->output)) {
				total_width += get_win_width(area.w, g, t->width) + g;
			}
		}

		state->scroll_offset += (int)(gesture->dx * 2.0);
		state->gesture_cumulative_dx += gesture->dx * 2.0;

		uint64_t now = get_time_ms();

		if (state->gesture_history_len < GOONWC_GESTURE_HISTORY_SIZE) {
			state->gesture_history[state->gesture_history_len++] = (goonwc_gesture_event){
				.dx = gesture->dx * 2.0,
				.timestamp_ms = now,
			};
		} else {
			memmove(&state->gesture_history[0], &state->gesture_history[1],
			        (GOONWC_GESTURE_HISTORY_SIZE - 1) * sizeof(goonwc_gesture_event));
			state->gesture_history[GOONWC_GESTURE_HISTORY_SIZE - 1] = (goonwc_gesture_event){
				.dx = gesture->dx * 2.0,
				.timestamp_ms = now,
			};
		}

		int min_scroll = -area.w;
		int max_scroll = total_width;
		if (state->scroll_offset < min_scroll) state->scroll_offset = min_scroll;
		if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;

		arrange_output(server, state->output, false);
	}
}

static double calculate_gesture_velocity(goonwc_output_state *state) {
	if (state->gesture_history_len < 2) return 0.0;

	uint64_t now = get_time_ms();
	uint64_t cutoff = now - GOONWC_GESTURE_HISTORY_MS;

	double total_delta = 0.0;
	uint64_t first_time = 0;
	uint64_t last_time = 0;
	bool found_first = false;

	for (int i = 0; i < state->gesture_history_len; i++) {
		goonwc_gesture_event *ev = &state->gesture_history[i];
		if (ev->timestamp_ms >= cutoff) {
			total_delta += ev->dx;
			if (!found_first) {
				first_time = ev->timestamp_ms;
				found_first = true;
			}
			last_time = ev->timestamp_ms;
		}
	}

	uint64_t time_diff = last_time - first_time;
	if (time_diff == 0) return 0.0;

	return (total_delta / (double)time_diff) * 1000.0;
}

#define DECELERATION_TOUCHPAD 0.997

static void on_gesture_end(owl_display *display, owl_gesture *gesture, void *data) {
	(void)display;
	goonwc_server *server = data;

	if (gesture->fingers == 3) {
		goonwc_output_state *state = get_active_state(server);
		if (!state || !state->output) return;

		state->gesture_active = false;

		rect area = get_usable_area(server, state->output);
		int g = server->config.gap;
		double velocity = calculate_gesture_velocity(state);

		double decel = 1000.0 * log(DECELERATION_TOUCHPAD);
		double projected_offset = state->scroll_offset - (velocity / decel);

		goonwc_toplevel *best = NULL;
		double best_snap = 0;
		double best_dist = 1e9;
		int pos = g;

		for (goonwc_toplevel *t = server->toplevels; t; t = t->next) {
			if (!is_tiled_on(t, state->output)) continue;

			int w = get_win_width(area.w, g, t->width);
			double snap = pos - g;

			double dist = fabs(snap - projected_offset);
			if (dist < best_dist) {
				best_dist = dist;
				best_snap = snap;
				best = t;
			}

			pos += w + g;
		}

		if (!best) {
			arrange_output(server, state->output, true);
			return;
		}

		if (best_snap < 0) best_snap = 0;

		state->focused = best;
		owl_window_focus(best->window);

		anim_start(state, server, state->scroll_offset, best_snap, velocity);
	}
}

static goonwc_output_config *find_output_config(goonwc_server *server, const char *name) {
	for (int i = 0; i < server->config.output_config_count; i++) {
		if (strcasecmp(server->config.output_configs[i].name, name) == 0) {
			return &server->config.output_configs[i];
		}
	}
	return NULL;
}

static void apply_output_config(goonwc_server *server, owl_output *output) {
	goonwc_output_config *cfg = find_output_config(server, output->name);
	if (!cfg) return;

	if (cfg->off) {
		fprintf(stderr, "goonwc: output %s disabled by config\n", output->name);
		return;
	}

	if (cfg->mode_set) {
		if (owl_output_set_mode(output, cfg->width, cfg->height, cfg->refresh)) {
			fprintf(stderr, "goonwc: output %s mode set to %dx%d\n",
				output->name, cfg->width, cfg->height);
		} else {
			fprintf(stderr, "goonwc: output %s mode %dx%d not found\n",
				output->name, cfg->width, cfg->height);
		}
	}

	if (cfg->position_set) {
		owl_output_set_position(output, cfg->x, cfg->y);
		fprintf(stderr, "goonwc: output %s position set to %d,%d\n",
			output->name, cfg->x, cfg->y);
	}
}

static void on_output_connect(owl_display *display, owl_output *output, void *data) {
	(void)display;
	goonwc_server *server = data;

	apply_output_config(server, output);

	if (server->output_count < GOONWC_MAX_OUTPUTS) {
		for (int i = 0; i < server->output_count; i++) {
			if (server->output_states[i].output == output) return;
		}
		server->output_states[server->output_count].output = output;
		server->output_states[server->output_count].scroll_offset = 0;
		server->output_states[server->output_count].focused = NULL;
		server->output_count++;

		if (!server->active_output) {
			server->active_output = output;
		}
	}
}

bool server_init(goonwc_server *server) {
	memset(server, 0, sizeof(goonwc_server));

	server->display = owl_display_create();
	if (!server->display) {
		fprintf(stderr, "goonwc: failed to create owl display\n");
		return false;
	}

	config_init(server, NULL);

	owl_set_keyboard_repeat(server->display, server->config.repeat_rate, server->config.repeat_delay);

	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_CREATE, on_window_create, server);
	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_DESTROY, on_window_destroy, server);
	owl_set_window_callback(server->display, OWL_WINDOW_EVENT_MAP, on_window_map, server);
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

	owl_set_output_callback(server->display, OWL_OUTPUT_EVENT_CONNECT, on_output_connect, server);

	owl_set_gesture_callback(server->display, OWL_GESTURE_SWIPE_BEGIN, on_gesture_begin, server);
	owl_set_gesture_callback(server->display, OWL_GESTURE_SWIPE_UPDATE, on_gesture_update, server);
	owl_set_gesture_callback(server->display, OWL_GESTURE_SWIPE_END, on_gesture_end, server);

	owl_set_render_callback(server->display, on_render_window, server);

	server->cursor_mode = GOONWC_CURSOR_PASSTHROUGH;
	server->tagset = 1; /* start on tag 1 */

	int output_count = 0;
	owl_output **outputs = owl_display_get_outputs(server->display, &output_count);
	server->output_count = output_count < GOONWC_MAX_OUTPUTS ? output_count : GOONWC_MAX_OUTPUTS;
	for (int i = 0; i < server->output_count; i++) {
		server->output_states[i].output = outputs[i];
		server->output_states[i].scroll_offset = 0;
		server->output_states[i].focused = NULL;
		apply_output_config(server, outputs[i]);
	}
	server->active_output = server->output_count > 0 ? outputs[0] : NULL;

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

void server_run(goonwc_server *server, const char *startup_cmd) {
	if (startup_cmd) {
		if (fork() == 0) {
			setenv("WAYLAND_DISPLAY", owl_display_get_socket(server->display), 1);
			setenv("XDG_SESSION_TYPE", "wayland", 1);
			setenv("XDG_CURRENT_DESKTOP", "goonwc", 1);
			setenv("TERM", "xterm-256color", 1);
			setenv("COLORTERM", "truecolor", 1);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			_exit(1);
		}
	}

	fprintf(stderr, "goonwc: running on %s\n", owl_display_get_socket(server->display));
	owl_display_run(server->display);
}

void server_cleanup(goonwc_server *server) {
	config_cleanup(server);
	owl_display_destroy(server->display);
}

static int focus_stack_remove(goonwc_server *server, goonwc_toplevel *toplevel) {
	for (int i = 0; i < server->focus_stack_len; i++) {
		if (server->focus_stack[i].toplevel == toplevel) {
			int saved_scroll = server->focus_stack[i].scroll_offset;
			memmove(&server->focus_stack[i], &server->focus_stack[i + 1],
			        (server->focus_stack_len - i - 1) * sizeof(goonwc_focus_entry));
			server->focus_stack_len--;
			return saved_scroll;
		}
	}
	goonwc_output_state *state = toplevel ? get_output_state(server, toplevel->output) : NULL;
	return state ? state->scroll_offset : 0;
}

static void focus_stack_push(goonwc_server *server, goonwc_toplevel *toplevel) {
	focus_stack_remove(server, toplevel);

	if (server->focus_stack_len >= GOONWC_FOCUS_STACK_SIZE) {
		memmove(&server->focus_stack[0], &server->focus_stack[1],
		        (GOONWC_FOCUS_STACK_SIZE - 1) * sizeof(goonwc_focus_entry));
		server->focus_stack_len = GOONWC_FOCUS_STACK_SIZE - 1;
	}

	goonwc_output_state *state = get_output_state(server, toplevel->output);
	server->focus_stack[server->focus_stack_len++] = (goonwc_focus_entry){
		.toplevel = toplevel,
		.scroll_offset = state ? state->scroll_offset : 0,
	};
}

static goonwc_focus_entry *focus_stack_get_previous(goonwc_server *server) {
	for (int i = server->focus_stack_len - 1; i >= 0; i--) {
		goonwc_focus_entry *entry = &server->focus_stack[i];
		if (entry->toplevel && is_visible(entry->toplevel)) {
			return entry;
		}
	}
	return NULL;
}

goonwc_toplevel *toplevel_create(goonwc_server *server, owl_window *window) {
	goonwc_toplevel *toplevel = calloc(1, sizeof(goonwc_toplevel));
	if (!toplevel) {
		return NULL;
	}

	toplevel->server = server;
	toplevel->window = window;
	toplevel->tags = server->tagset; /* new windows get current tag */
	toplevel->is_fullscreen = false;
	toplevel->preset_index = 1; /* start at 1/2 width */
	toplevel->width.type = GOONWC_WIDTH_PROPORTION;
	toplevel->width.value = goonwc_width_presets[1]; /* 0.5 */
	toplevel->output = server->active_output;

	/* insert after focused window on this output (to the right in scroll layout) */
	goonwc_output_state *state = get_output_state(server, toplevel->output);
	goonwc_toplevel *focused = state ? state->focused : NULL;

	if (focused) {
		toplevel->prev = focused;
		toplevel->next = focused->next;
		if (focused->next) {
			focused->next->prev = toplevel;
		}
		focused->next = toplevel;
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

void toplevel_destroy(goonwc_toplevel *toplevel) {
	if (!toplevel) {
		return;
	}

	goonwc_server *server = toplevel->server;
	goonwc_output_state *state = get_output_state(server, toplevel->output);

	int saved_scroll = focus_stack_remove(server, toplevel);

	if (state && state->focused == toplevel) {
		state->focused = NULL;
		goonwc_focus_entry *entry = focus_stack_get_previous(server);
		if (entry && entry->toplevel->output == toplevel->output) {
			state->focused = entry->toplevel;
			state->scroll_offset = saved_scroll;
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

void toplevel_focus(goonwc_toplevel *toplevel) {
	if (!toplevel) {
		return;
	}

	goonwc_server *server = toplevel->server;
	goonwc_output_state *state = get_output_state(server, toplevel->output);

	if (state && state->focused == toplevel) {
		return;
	}

	if (state) {
		state->focused = toplevel;
	}
	focus_stack_push(server, toplevel);
	owl_window_focus(toplevel->window);
}

goonwc_toplevel *toplevel_at(goonwc_server *server, int x, int y) {
	goonwc_toplevel *toplevel;
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

bool handle_keybinding(goonwc_server *server, uint32_t keysym, uint32_t modifiers) {
	uint32_t relevant_mods = OWL_MOD_SHIFT | OWL_MOD_CTRL | OWL_MOD_ALT | OWL_MOD_SUPER;
	uint32_t cleaned_mods = modifiers & relevant_mods;

	for (int i = 0; i < server->config.keybind_count; i++) {
		goonwc_keybind *kb = &server->config.keybinds[i];
		if (kb->key == keysym && kb->mods == cleaned_mods) {
			switch (kb->action) {
			case ACTION_SPAWN:
				spawn_cmd(server, kb->arg.cmd);
				break;
			case ACTION_SPAWN_TERMINAL:
				spawn_terminal(server);
				break;
			case ACTION_KILL:
				killclient(server);
				break;
			case ACTION_QUIT:
				owl_display_terminate(server->display);
				break;
			case ACTION_RELOAD_CONFIG:
				config_reload(server);
				break;
			case ACTION_FOCUS_NEXT:
				focus_next(server);
				break;
			case ACTION_FOCUS_PREV:
				focus_prev(server);
				break;
			case ACTION_TOGGLE_WIDTH:
				toggle_width(server, kb->arg.i);
				break;
			case ACTION_MAXIMIZE:
				maximize(server);
				break;
			case ACTION_TOGGLE_FLOATING:
				togglefloating(server);
				break;
			case ACTION_VIEW_TAG:
				view_tag(server, kb->arg.i);
				break;
			case ACTION_MOVE_TO_TAG:
				move_to_tag(server, kb->arg.i);
				break;
			case ACTION_TOGGLE_VIEW_TAG:
				toggle_view_tag(server, kb->arg.i);
				break;
			case ACTION_TOGGLE_TAG:
				toggle_tag(server, kb->arg.i);
				break;
			case ACTION_NONE:
				break;
			}
			return true;
		}
	}

	return false;
}

void begin_interactive(goonwc_toplevel *toplevel, goonwc_cursor_mode mode, uint32_t edges) {
	goonwc_server *server = toplevel->server;

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

void reset_cursor_mode(goonwc_server *server) {
	server->cursor_mode = GOONWC_CURSOR_PASSTHROUGH;
	server->grabbed_tiled = NULL;
}

void process_cursor_move(goonwc_server *server, int x, int y) {
	goonwc_toplevel *toplevel = server->grabbed_tiled;
	if (!toplevel) {
		return;
	}

	int new_x = server->grab_pos_x + (x - (int)server->grab_x);
	int new_y = server->grab_pos_y + (y - (int)server->grab_y);

	owl_window_move(toplevel->window, new_x, new_y);
}

void process_cursor_resize(goonwc_server *server, int x, int y) {
	goonwc_toplevel *toplevel = server->grabbed_tiled;
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
