#define _GNU_SOURCE
#include "config.h"
#include "types.h"
#include "goon.h"
#include <owl/owl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/inotify.h>

enum field_type { FIELD_INT, FIELD_COLOR, FIELD_STRING };

static const struct {
	const char *name;
	size_t offset;
	enum field_type type;
} config_fields[] = {
	{"gap",              offsetof(dwc_config, gap),              FIELD_INT},
	{"border_width",     offsetof(dwc_config, border_width),     FIELD_INT},
	{"repeat_rate",      offsetof(dwc_config, repeat_rate),      FIELD_INT},
	{"repeat_delay",     offsetof(dwc_config, repeat_delay),     FIELD_INT},
	{"border_focused",   offsetof(dwc_config, border_focused),   FIELD_COLOR},
	{"border_unfocused", offsetof(dwc_config, border_unfocused), FIELD_COLOR},
	{"terminal",         offsetof(dwc_config, terminal),         FIELD_STRING},
	{"startup",          offsetof(dwc_config, startup_cmd),      FIELD_STRING},
};

static const struct {
	const char *name;
	uint32_t mod;
} mod_map[] = {
	{"super",   OWL_MOD_SUPER},
	{"alt",     OWL_MOD_ALT},
	{"shift",   OWL_MOD_SHIFT},
	{"ctrl",    OWL_MOD_CTRL},
	{"control", OWL_MOD_CTRL},
};

enum arg_type { ARG_NONE, ARG_INT, ARG_STRING };

static const struct {
	const char *name;
	dwc_action action;
	enum arg_type arg_type;
} action_map[] = {
	{"spawn",            ACTION_SPAWN,            ARG_STRING},
	{"spawn-terminal",   ACTION_SPAWN_TERMINAL,   ARG_NONE},
	{"kill",             ACTION_KILL,             ARG_NONE},
	{"quit",             ACTION_QUIT,             ARG_NONE},
	{"reload-config",    ACTION_RELOAD_CONFIG,    ARG_NONE},
	{"focus-next",       ACTION_FOCUS_NEXT,       ARG_NONE},
	{"focus-prev",       ACTION_FOCUS_PREV,       ARG_NONE},
	{"toggle-width",     ACTION_TOGGLE_WIDTH,     ARG_INT},
	{"maximize",         ACTION_MAXIMIZE,         ARG_NONE},
	{"toggle-floating",  ACTION_TOGGLE_FLOATING,  ARG_NONE},
	{"view-tag",         ACTION_VIEW_TAG,         ARG_INT},
	{"move-to-tag",      ACTION_MOVE_TO_TAG,      ARG_INT},
	{"toggle-view-tag",  ACTION_TOGGLE_VIEW_TAG,  ARG_INT},
	{"toggle-tag",       ACTION_TOGGLE_TAG,       ARG_INT},
};

static void config_set_defaults(dwc_config *config) {
	config->gap = 5;
	config->border_width = 2;
	config->border_focused[0] = 0.57f;
	config->border_focused[1] = 0.63f;
	config->border_focused[2] = 0.80f;
	config->border_focused[3] = 1.0f;
	config->border_unfocused[0] = 0.30f;
	config->border_unfocused[1] = 0.30f;
	config->border_unfocused[2] = 0.30f;
	config->border_unfocused[3] = 1.0f;
	config->repeat_rate = 35;
	config->repeat_delay = 200;
	config->terminal = NULL;
	config->startup_cmd = NULL;
	config->window_rule_count = 0;
	config->keybind_count = 0;
}

static void config_free_strings(dwc_config *config) {
	for (int i = 0; i < config->window_rule_count; i++) {
		free(config->window_rules[i].app_id);
		free(config->window_rules[i].title);
	}
	for (int i = 0; i < config->keybind_count; i++) {
		if (config->keybinds[i].action == ACTION_SPAWN)
			free(config->keybinds[i].arg.cmd);
	}
	free(config->terminal);
	free(config->startup_cmd);
}

static void parse_color(Goon_Value *val, float *out) {
	if (!val || !goon_is_string(val)) return;
	const char *str = goon_to_string(val);
	if (str[0] == '#' && strlen(str) == 7) {
		unsigned int r, g, b;
		sscanf(str + 1, "%02x%02x%02x", &r, &g, &b);
		out[0] = r / 255.0f;
		out[1] = g / 255.0f;
		out[2] = b / 255.0f;
		out[3] = 1.0f;
	}
}

static uint32_t parse_mods(Goon_Value *list) {
	if (!list || !goon_is_list(list)) return 0;
	uint32_t mods = 0;
	size_t len = goon_list_len(list);
	for (size_t i = 0; i < len; i++) {
		Goon_Value *item = goon_list_get(list, i);
		if (!item || !goon_is_string(item)) continue;
		const char *name = goon_to_string(item);
		for (size_t j = 0; j < sizeof(mod_map)/sizeof(mod_map[0]); j++) {
			if (strcasecmp(name, mod_map[j].name) == 0) {
				mods |= mod_map[j].mod;
				break;
			}
		}
	}
	return mods;
}

static dwc_action parse_action(const char *name, enum arg_type *arg_type_out) {
	for (size_t i = 0; i < sizeof(action_map)/sizeof(action_map[0]); i++) {
		if (strcmp(name, action_map[i].name) == 0) {
			*arg_type_out = action_map[i].arg_type;
			return action_map[i].action;
		}
	}
	*arg_type_out = ARG_NONE;
	return ACTION_NONE;
}

static void parse_keybinds(Goon_Value *keys, dwc_config *config) {
	if (!keys || !goon_is_list(keys)) return;
	size_t len = goon_list_len(keys);

	for (size_t i = 0; i < len && config->keybind_count < DWC_MAX_KEYBINDS; i++) {
		Goon_Value *kb = goon_list_get(keys, i);
		if (!kb || !goon_is_record(kb)) continue;

		Goon_Value *mod_val = goon_record_get(kb, "mod");
		Goon_Value *key_val = goon_record_get(kb, "key");
		Goon_Value *action_val = goon_record_get(kb, "action");

		if (!key_val || !goon_is_string(key_val)) continue;
		if (!action_val || !goon_is_string(action_val)) continue;

		const char *key_str = goon_to_string(key_val);
		xkb_keysym_t keysym = xkb_keysym_from_name(key_str, 0);
		if (keysym == XKB_KEY_NoSymbol) continue;

		enum arg_type arg_type;
		dwc_action action = parse_action(goon_to_string(action_val), &arg_type);
		if (action == ACTION_NONE) continue;

		dwc_keybind *bind = &config->keybinds[config->keybind_count++];
		bind->mods = parse_mods(mod_val);
		bind->key = keysym;
		bind->action = action;
		bind->arg.i = 0;

		Goon_Value *arg_val = goon_record_get(kb, "arg");
		if (arg_val) {
			if (arg_type == ARG_INT && goon_is_int(arg_val))
				bind->arg.i = (int32_t)goon_to_int(arg_val);
			else if (arg_type == ARG_STRING && goon_is_string(arg_val))
				bind->arg.cmd = strdup(goon_to_string(arg_val));
		}
	}
}

static const char *shifted_keys[] = {
	NULL, "exclam", "at", "numbersign", "dollar",
	"percent", "asciicircum", "ampersand", "asterisk", "parenleft"
};

static bool mods_contain_shift(Goon_Value *mods) {
	if (!mods || !goon_is_list(mods)) return false;
	for (size_t i = 0; i < goon_list_len(mods); i++) {
		Goon_Value *m = goon_list_get(mods, i);
		if (m && goon_is_string(m) && strcasecmp(goon_to_string(m), "shift") == 0)
			return true;
	}
	return false;
}

static Goon_Value *builtin_tag_binds(Goon_Ctx *ctx, Goon_Value **args, size_t argc) {
	if (argc != 4) return goon_list(ctx);

	Goon_Value *mods = args[0];
	Goon_Value *action = args[1];
	Goon_Value *start_val = args[2];
	Goon_Value *end_val = args[3];

	if (!goon_is_list(mods) || !goon_is_string(action) ||
	    !goon_is_int(start_val) || !goon_is_int(end_val))
		return goon_list(ctx);

	int start = (int)goon_to_int(start_val);
	int end = (int)goon_to_int(end_val);
	bool use_shifted = mods_contain_shift(mods);

	Goon_Value *result = goon_list(ctx);

	for (int n = start; n <= end && n >= 1 && n <= 9; n++) {
		Goon_Value *rec = goon_record(ctx);

		goon_record_set(ctx, rec, "mod", mods);
		goon_record_set(ctx, rec, "action", action);
		goon_record_set(ctx, rec, "arg", goon_int(ctx, n - 1));

		if (use_shifted) {
			goon_record_set(ctx, rec, "key", goon_string(ctx, shifted_keys[n]));
		} else {
			char key_str[2] = { '0' + n, '\0' };
			goon_record_set(ctx, rec, "key", goon_string(ctx, key_str));
		}

		goon_list_push(ctx, result, rec);
	}

	return result;
}

static void parse_window_rules(Goon_Value *rules, dwc_config *config) {
	if (!rules || !goon_is_list(rules)) return;
	size_t len = goon_list_len(rules);

	for (size_t i = 0; i < len && config->window_rule_count < DWC_MAX_WINDOW_RULES; i++) {
		Goon_Value *rule = goon_list_get(rules, i);
		if (!rule || !goon_is_record(rule)) continue;

		Goon_Value *match = goon_record_get(rule, "match");
		if (!match || !goon_is_record(match)) continue;

		dwc_window_rule *wr = &config->window_rules[config->window_rule_count++];
		memset(wr, 0, sizeof(*wr));

		Goon_Value *app_id = goon_record_get(match, "app_id");
		if (app_id && goon_is_string(app_id))
			wr->app_id = strdup(goon_to_string(app_id));

		Goon_Value *title = goon_record_get(match, "title");
		if (title && goon_is_string(title))
			wr->title = strdup(goon_to_string(title));

		Goon_Value *block = goon_record_get(rule, "block_screen_capture");
		if (block && goon_is_bool(block))
			wr->block_screen_capture = goon_to_bool(block);
	}
}

static bool config_load_goon(dwc_server *server) {
	Goon_Ctx *ctx = goon_create();
	if (!ctx) return false;

	goon_register(ctx, "tag_binds", builtin_tag_binds);

	if (!goon_load_file(ctx, server->config_path)) {
		fprintf(stderr, "dwc: config error: %s\n", goon_get_error(ctx));
		goon_destroy(ctx);
		return false;
	}

	Goon_Value *root = goon_eval_result(ctx);
	if (!root || !goon_is_record(root)) {
		fprintf(stderr, "dwc: config must evaluate to a record\n");
		goon_destroy(ctx);
		return false;
	}

	config_free_strings(&server->config);
	config_set_defaults(&server->config);

	for (size_t i = 0; i < sizeof(config_fields)/sizeof(config_fields[0]); i++) {
		Goon_Value *val = goon_record_get(root, config_fields[i].name);
		if (!val) continue;

		void *dst = (char *)&server->config + config_fields[i].offset;
		switch (config_fields[i].type) {
		case FIELD_INT:
			if (goon_is_int(val)) *(int *)dst = (int)goon_to_int(val);
			break;
		case FIELD_COLOR:
			parse_color(val, dst);
			break;
		case FIELD_STRING:
			if (goon_is_string(val)) *(char **)dst = strdup(goon_to_string(val));
			break;
		}
	}

	parse_window_rules(goon_record_get(root, "window_rules"), &server->config);
	parse_keybinds(goon_record_get(root, "keys"), &server->config);

	goon_destroy(ctx);
	fprintf(stderr, "dwc: loaded config (gap=%d, border=%d, keybinds=%d)\n",
		server->config.gap, server->config.border_width, server->config.keybind_count);
	return true;
}

static int on_config_change(int fd, uint32_t mask, void *data) {
	dwc_server *server = data;

	char buf[4096];
	ssize_t len = read(fd, buf, sizeof(buf));
	if (len <= 0) return 0;

	struct inotify_event *event;
	for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
		event = (struct inotify_event *)ptr;

		if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
			inotify_rm_watch(server->inotify_fd, server->inotify_wd);
			usleep(50000);
			server->inotify_wd = inotify_add_watch(server->inotify_fd, server->config_path,
				IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF);
			if (server->inotify_wd < 0) {
				fprintf(stderr, "dwc: failed to re-add config watch\n");
			}
		}

		if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF)) {
			fprintf(stderr, "dwc: config file changed, reloading\n");
			config_reload(server);
		}
	}
	return 0;
}

static char *get_default_config_path(void) {
	const char *home = getenv("HOME");
	if (!home) return NULL;

	char *path = NULL;
	if (asprintf(&path, "%s/.config/dwc/config.goon", home) < 0)
		return NULL;
	return path;
}

bool config_init(dwc_server *server, const char *path) {
	config_set_defaults(&server->config);
	server->inotify_fd = -1;
	server->inotify_wd = -1;
	server->config_event_source = NULL;

	if (path) {
		server->config_path = strdup(path);
	} else {
		server->config_path = get_default_config_path();
	}

	if (!server->config_path) {
		fprintf(stderr, "dwc: could not determine config path\n");
		return false;
	}

	if (access(server->config_path, R_OK) != 0) {
		fprintf(stderr, "dwc: config file not found: %s, using defaults\n", server->config_path);
		return true;
	}

	if (!config_load_goon(server)) {
		fprintf(stderr, "dwc: failed to load config, using defaults\n");
	}

	server->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (server->inotify_fd < 0) {
		fprintf(stderr, "dwc: inotify_init failed, hot reload disabled\n");
		return true;
	}

	server->inotify_wd = inotify_add_watch(
        server->inotify_fd,
        server->config_path,
        IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF
    );
	if (server->inotify_wd < 0) {
		fprintf(stderr, "dwc: inotify_add_watch failed\n");
		close(server->inotify_fd);
		server->inotify_fd = -1;
		return true;
	}

	struct wl_event_loop *loop = owl_display_get_event_loop(server->display);
	server->config_event_source = wl_event_loop_add_fd(
        loop,
        server->inotify_fd,
        WL_EVENT_READABLE,
        on_config_change,
        server
    );

	return true;
}

void config_cleanup(dwc_server *server) {
	if (server->config_event_source) {
		wl_event_source_remove(server->config_event_source);
		server->config_event_source = NULL;
	}
	if (server->inotify_wd >= 0) {
		inotify_rm_watch(server->inotify_fd, server->inotify_wd);
		server->inotify_wd = -1;
	}
	if (server->inotify_fd >= 0) {
		close(server->inotify_fd);
		server->inotify_fd = -1;
	}
	config_free_strings(&server->config);
	free(server->config_path);
	server->config_path = NULL;
}

bool config_reload(dwc_server *server) {
	if (!server->config_path) return false;
	return config_load_goon(server);
}
