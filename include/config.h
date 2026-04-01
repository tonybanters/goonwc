#ifndef GOONWC_CONFIG_H
#define GOONWC_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include <owl/owl.h>

typedef struct goonwc_server goonwc_server;

#define GOONWC_MAX_WINDOW_RULES 64
#define GOONWC_MAX_KEYBINDS 128

typedef enum {
	ACTION_NONE,
	ACTION_SPAWN,
	ACTION_SPAWN_TERMINAL,
	ACTION_KILL,
	ACTION_QUIT,
	ACTION_RELOAD_CONFIG,
	ACTION_FOCUS_NEXT,
	ACTION_FOCUS_PREV,
	ACTION_TOGGLE_WIDTH,
	ACTION_MAXIMIZE,
	ACTION_TOGGLE_FLOATING,
	ACTION_VIEW_TAG,
	ACTION_MOVE_TO_TAG,
	ACTION_TOGGLE_VIEW_TAG,
	ACTION_TOGGLE_TAG,
} goonwc_action;

typedef struct {
	uint32_t mods;
	xkb_keysym_t key;
	goonwc_action action;
	union {
		char *cmd;
		int32_t i;
	} arg;
} goonwc_keybind;

typedef struct {
	char *app_id;
	char *title;
	bool block_screen_capture;
} goonwc_window_rule;

typedef struct {
	int gap;
	int border_width;
	float border_focused[4];
	float border_unfocused[4];
	int repeat_rate;
	int repeat_delay;

	char *terminal;
	char *startup_cmd;

	goonwc_window_rule window_rules[GOONWC_MAX_WINDOW_RULES];
	int window_rule_count;

	goonwc_keybind keybinds[GOONWC_MAX_KEYBINDS];
	int keybind_count;
} goonwc_config;

bool config_init(goonwc_server *server, const char *path);
void config_cleanup(goonwc_server *server);
bool config_reload(goonwc_server *server);

#endif
