#ifndef DWC_CONFIG_H
#define DWC_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include <owl/owl.h>

typedef struct dwc_server dwc_server;

#define DWC_MAX_WINDOW_RULES 64
#define DWC_MAX_KEYBINDS 128

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
} dwc_action;

typedef struct {
	uint32_t mods;
	xkb_keysym_t key;
	dwc_action action;
	union {
		char *cmd;
		int32_t i;
	} arg;
} dwc_keybind;

typedef struct {
	char *app_id;
	char *title;
	bool block_screen_capture;
} dwc_window_rule;

typedef struct {
	int gap;
	int border_width;
	float border_focused[4];
	float border_unfocused[4];
	int repeat_rate;
	int repeat_delay;

	char *terminal;
	char *startup_cmd;

	dwc_window_rule window_rules[DWC_MAX_WINDOW_RULES];
	int window_rule_count;

	dwc_keybind keybinds[DWC_MAX_KEYBINDS];
	int keybind_count;
} dwc_config;

bool config_init(dwc_server *server, const char *path);
void config_cleanup(dwc_server *server);
bool config_reload(dwc_server *server);

#endif
