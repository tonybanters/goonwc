#ifndef OWL_H
#define OWL_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

/* Forward declarations for internal types */
struct wl_resource;
struct wl_global;
struct wl_display;
struct wl_event_loop;
struct gbm_surface;
struct gbm_bo;
struct gbm_device;
struct libinput;
struct udev;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct wl_event_source;

/* Enums */
typedef enum {
	OWL_LAYER_BACKGROUND = 0,
	OWL_LAYER_BOTTOM = 1,
	OWL_LAYER_TOP = 2,
	OWL_LAYER_OVERLAY = 3,
} owl_layer;

typedef enum {
	OWL_ANCHOR_TOP = 1,
	OWL_ANCHOR_BOTTOM = 2,
	OWL_ANCHOR_LEFT = 4,
	OWL_ANCHOR_RIGHT = 8,
	OWL_ANCHOR_TOP_EDGE = OWL_ANCHOR_TOP | OWL_ANCHOR_LEFT | OWL_ANCHOR_RIGHT,
	OWL_ANCHOR_BOTTOM_EDGE = OWL_ANCHOR_BOTTOM | OWL_ANCHOR_LEFT | OWL_ANCHOR_RIGHT,
	OWL_ANCHOR_LEFT_EDGE = OWL_ANCHOR_LEFT | OWL_ANCHOR_TOP | OWL_ANCHOR_BOTTOM,
	OWL_ANCHOR_RIGHT_EDGE = OWL_ANCHOR_RIGHT | OWL_ANCHOR_TOP | OWL_ANCHOR_BOTTOM,
} owl_anchor;

typedef enum {
	OWL_KEYBOARD_INTERACTIVITY_NONE = 0,
	OWL_KEYBOARD_INTERACTIVITY_EXCLUSIVE = 1,
	OWL_KEYBOARD_INTERACTIVITY_ON_DEMAND = 2,
} owl_keyboard_interactivity;

typedef enum {
	OWL_MOD_SHIFT = 1,
	OWL_MOD_CTRL = 2,
	OWL_MOD_ALT = 4,
	OWL_MOD_SUPER = 8,
} owl_modifier;

typedef enum {
	OWL_WINDOW_EVENT_CREATE,
	OWL_WINDOW_EVENT_DESTROY,
	OWL_WINDOW_EVENT_MAP,
	OWL_WINDOW_EVENT_UNMAP,
	OWL_WINDOW_EVENT_FOCUS,
	OWL_WINDOW_EVENT_UNFOCUS,
	OWL_WINDOW_EVENT_FULLSCREEN,
	OWL_WINDOW_EVENT_TITLE_CHANGE,
	OWL_WINDOW_EVENT_REQUEST_MOVE,
	OWL_WINDOW_EVENT_REQUEST_RESIZE,
} owl_window_event;

typedef enum {
	OWL_INPUT_EVENT_KEY_PRESS,
	OWL_INPUT_EVENT_KEY_RELEASE,
	OWL_INPUT_EVENT_BUTTON_PRESS,
	OWL_INPUT_EVENT_BUTTON_RELEASE,
	OWL_INPUT_EVENT_POINTER_MOTION,
} owl_input_event;

typedef enum {
	OWL_OUTPUT_EVENT_CONNECT,
	OWL_OUTPUT_EVENT_DISCONNECT,
	OWL_OUTPUT_EVENT_MODE_CHANGE,
} owl_output_event;

typedef enum {
	OWL_LAYER_SURFACE_EVENT_CREATE,
	OWL_LAYER_SURFACE_EVENT_DESTROY,
	OWL_LAYER_SURFACE_EVENT_MAP,
	OWL_LAYER_SURFACE_EVENT_UNMAP,
} owl_layer_surface_event;

typedef enum {
	OWL_WORKSPACE_STATE_ACTIVE = 1,
	OWL_WORKSPACE_STATE_URGENT = 2,
	OWL_WORKSPACE_STATE_HIDDEN = 4,
} owl_workspace_state;

typedef enum {
	OWL_WORKSPACE_EVENT_ACTIVATE,
	OWL_WORKSPACE_EVENT_DEACTIVATE,
	OWL_WORKSPACE_EVENT_REMOVE,
} owl_workspace_event;

typedef enum {
	OWL_GESTURE_SWIPE_BEGIN,
	OWL_GESTURE_SWIPE_UPDATE,
	OWL_GESTURE_SWIPE_END,
} owl_gesture_event;

typedef enum {
	OWL_RENDER_TARGET_OUTPUT,
	OWL_RENDER_TARGET_SCREENCAST,
	OWL_RENDER_TARGET_SCREENSHOT,
} owl_render_target;

typedef enum {
	OWL_BLOCK_OUT_NONE,
	OWL_BLOCK_OUT_SCREENCAST,
	OWL_BLOCK_OUT_SCREEN_CAPTURE,
} owl_block_out_from;

/* Gesture data */
typedef struct owl_gesture {
	int fingers;
	double dx;
	double dy;
} owl_gesture;

/* Internal types - forward declared */
typedef struct owl_surface owl_surface;
typedef struct owl_shm_pool owl_shm_pool;
typedef struct owl_shm_buffer owl_shm_buffer;
typedef struct owl_display owl_display;

/* Input event data - all fields public */
typedef struct owl_input {
	uint32_t keycode;
	uint32_t keysym;
	uint32_t modifiers;
	uint32_t button;
	int pointer_x;
	int pointer_y;
} owl_input;

/* output - public fields first, then internal */
typedef struct owl_output {
	/* Public - read these directly */
	int x;
	int y;
	int width;
	int height;
	char *name;

	/* Internal - don't touch */
	owl_display *display;
	uint32_t drm_connector_id;
	uint32_t drm_crtc_id;
	char drm_mode[68]; /* drmModeModeInfo */
	struct gbm_surface *gbm_surface;
	void *egl_surface;
	uint32_t framebuffer_id;
	struct gbm_bo *current_bo;
	struct gbm_bo *next_bo;
	bool page_flip_pending;
	struct wl_global *wl_output_global;
} owl_output;

/* window - public fields first, then internal */
typedef struct owl_window {
	/* Public - read/write these directly */
	int x;
	int y;
	int width;
	int height;
	char *title;
	char *app_id;
	bool fullscreen;
	bool focused;
	bool mapped;
	bool tiled;
	owl_block_out_from block_out_from;
	void *user_data;

	/* internal - don't touch */
	owl_display *display;
	owl_surface *surface;
	struct wl_resource *xdg_surface_resource;
	struct wl_resource *xdg_toplevel_resource;
	int geometry_x;
	int geometry_y;
	int geometry_width;
	int geometry_height;
	uint32_t pending_serial;
	bool pending_configure;
	struct wl_list link;
} owl_window;

/* layer surface - public fields first, then internal */
typedef struct owl_layer_surface {
	/* public - read these directly */
	owl_layer layer;
	uint32_t anchor;
	int32_t exclusive_zone;
	int32_t margin_top;
	int32_t margin_right;
	int32_t margin_bottom;
	int32_t margin_left;
	owl_keyboard_interactivity keyboard_interactivity;
	int32_t width;
	int32_t height;
	char *namespace;
	bool mapped;

	/* internal - don't touch */
	owl_display *display;
	owl_surface *surface;
	struct wl_resource *layer_surface_resource;
	owl_output *output;
	int32_t configured_width;
	int32_t configured_height;
	uint32_t pending_serial;
	bool initial_configure_sent;
} owl_layer_surface;

/* Workspace - public fields first, then internal */
typedef struct owl_workspace {
	/* Public - read these directly */
	char *name;
	uint32_t state;
	int32_t coordinate;

	/* Internal - don't touch */
	owl_display *display;
	char *id;
	struct wl_list resources;
	struct wl_list link;
} owl_workspace;

/* Callbacks */
typedef void (*owl_window_callback)(owl_display *display, owl_window *window, void *data);
typedef bool (*owl_input_callback)(owl_display *display, owl_input *input, void *data);
typedef void (*owl_output_callback)(owl_display *display, owl_output *output, void *data);
typedef void (*owl_layer_surface_callback)(owl_display *display, owl_layer_surface *surface, void *data);
typedef void (*owl_workspace_callback)(owl_display *display, owl_workspace *workspace, void *data);
typedef void (*owl_gesture_callback)(owl_display *display, owl_gesture *gesture, void *data);
typedef void (*owl_render_callback)(owl_display *display, owl_window *window, void *data);

/* Display - kept opaque, too much internal state */
owl_display *owl_display_create(void);
void owl_display_destroy(owl_display *display);
void owl_display_run(owl_display *display);
void owl_display_terminate(owl_display *display);
const char *owl_display_get_socket(owl_display *display);
void owl_display_get_pointer(owl_display *display, int *x, int *y);
owl_window **owl_display_get_windows(owl_display *display, int *count);
owl_output **owl_display_get_outputs(owl_display *display, int *count);
owl_layer_surface **owl_display_get_layer_surfaces(owl_display *display, int *count);
struct wl_event_loop *owl_display_get_event_loop(owl_display *display);
void owl_display_request_frame(owl_display *display);

/* Window actions - these actually do things */
void owl_window_focus(owl_window *window);
void owl_window_move(owl_window *window, int x, int y);
void owl_window_resize(owl_window *window, int width, int height);
void owl_window_close(owl_window *window);
void owl_window_set_fullscreen(owl_window *window, bool fullscreen);
void owl_window_set_tiled(owl_window *window, bool tiled);

/* Workspace actions */
owl_workspace *owl_workspace_create(owl_display *display, const char *name);
void owl_workspace_destroy(owl_workspace *workspace);
void owl_workspace_set_state(owl_workspace *workspace, uint32_t state);
void owl_workspace_set_coordinates(owl_workspace *workspace, int32_t x);
void owl_workspace_commit(owl_display *display);

/* Callbacks */
void owl_set_window_callback(owl_display *display, owl_window_event type, owl_window_callback callback, void *data);
void owl_set_input_callback(owl_display *display, owl_input_event type, owl_input_callback callback, void *data);
void owl_set_output_callback(owl_display *display, owl_output_event type, owl_output_callback callback, void *data);
void owl_set_layer_surface_callback(owl_display *display, owl_layer_surface_event type, owl_layer_surface_callback callback, void *data);
void owl_set_workspace_callback(owl_display *display, owl_workspace_event type, owl_workspace_callback callback, void *data);
void owl_set_gesture_callback(owl_display *display, owl_gesture_event type, owl_gesture_callback callback, void *data);

/* Rendering primitives */
void owl_render_rect(int x, int y, int w, int h, float r, float g, float b, float a);
void owl_set_render_callback(owl_display *display, owl_render_callback callback, void *data);

/* Keyboard settings */
void owl_set_keyboard_repeat(owl_display *display, int32_t rate, int32_t delay);

/* Session lock */
bool owl_display_is_locked(owl_display *display);

/* Capture block */
void owl_window_set_block_out_from(owl_window *window, owl_block_out_from mode);

#endif
