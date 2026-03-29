#ifndef OWL_INTERNAL_H
#define OWL_INTERNAL_H

#include <owl/owl.h>
#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>
#include <xf86drmMode.h>
#include <stdbool.h>
#include <stdint.h>

#define OWL_MAX_OUTPUTS 8
#define OWL_MAX_WINDOWS 256
#define OWL_MAX_CALLBACKS 16
#define OWL_MAX_WORKSPACES 32
#define OWL_MAX_LAYER_SURFACES 64
#define OWL_MAX_DMABUF_PLANES 4
#define OWL_MAX_DRM_FORMATS 64

typedef enum owl_buffer_type {
	OWL_BUFFER_SHM,
	OWL_BUFFER_DMABUF,
} owl_buffer_type;

typedef struct owl_drm_format {
	uint32_t format;
	uint64_t *modifiers;
	int modifier_count;
} owl_drm_format;

/* Internal types not exposed in owl.h */

typedef struct owl_shm_pool {
	owl_display *display;
	struct wl_resource *resource;
	int fd;
	void *data;
	int32_t size;
	int ref_count;
} owl_shm_pool;

typedef struct owl_shm_buffer {
	struct wl_resource *resource;
	owl_shm_pool *pool;
	int32_t offset;
	int32_t width;
	int32_t height;
	int32_t stride;
	uint32_t format;
	bool busy;
} owl_shm_buffer;

typedef struct owl_dmabuf_buffer {
	struct wl_resource *resource;
	owl_display *display;
	int32_t width;
	int32_t height;
	uint32_t format;
	uint32_t flags;
	int plane_count;
	int fds[OWL_MAX_DMABUF_PLANES];
	uint32_t offsets[OWL_MAX_DMABUF_PLANES];
	uint32_t strides[OWL_MAX_DMABUF_PLANES];
	uint64_t modifier;
	void *egl_image;
	uint32_t texture_id;
	bool failed;
} owl_dmabuf_buffer;

typedef struct owl_dmabuf_params {
	owl_display *display;
	struct wl_resource *resource;
	int fds[OWL_MAX_DMABUF_PLANES];
	uint32_t offsets[OWL_MAX_DMABUF_PLANES];
	uint32_t strides[OWL_MAX_DMABUF_PLANES];
	uint64_t modifier;
	int plane_count;
	bool planes_set[OWL_MAX_DMABUF_PLANES];
	bool used;
} owl_dmabuf_params;

typedef struct owl_viewport {
	struct wl_resource *resource;
	struct owl_surface *surface;
} owl_viewport;

typedef struct owl_viewport_state {
	bool has_src;
	double src_x, src_y, src_width, src_height;
	bool has_dst;
	int32_t dst_width, dst_height;
} owl_viewport_state;

typedef struct owl_surface_state {
	owl_buffer_type buffer_type;
	void *buffer;
	owl_viewport_state viewport;
	int32_t buffer_x;
	int32_t buffer_y;
	bool buffer_attached;
	struct wl_list frame_callbacks;
	int32_t damage_x;
	int32_t damage_y;
	int32_t damage_width;
	int32_t damage_height;
	bool has_damage;
} owl_surface_state;

typedef struct owl_surface {
	owl_display *display;
	struct wl_resource *resource;
	owl_surface_state pending;
	owl_surface_state current;
	uint32_t texture_id;
	int32_t texture_width;
	int32_t texture_height;
	bool has_content;
	struct wl_list link;
	owl_viewport *viewport;
} owl_surface;

typedef struct owl_frame_callback {
	struct wl_resource *resource;
	struct wl_list link;
} owl_frame_callback;

typedef struct {
	owl_window_callback callback;
	void *data;
} window_callback_entry;

typedef struct {
	owl_input_callback callback;
	void *data;
} input_callback_entry;

typedef struct {
	owl_output_callback callback;
	void *data;
} output_callback_entry;

typedef struct {
	owl_layer_surface_callback callback;
	void *data;
} layer_surface_callback_entry;

typedef struct {
	owl_workspace_callback callback;
	void *data;
} workspace_callback_entry;

typedef struct {
	owl_gesture_callback callback;
	void *data;
} gesture_callback_entry;

typedef enum {
	OWL_SCREENCOPY_FRAME_PENDING,
	OWL_SCREENCOPY_FRAME_COPYING,
	OWL_SCREENCOPY_FRAME_FAILED,
} owl_screencopy_frame_state;

typedef struct owl_screencopy_frame {
	owl_display *display;
	struct wl_resource *resource;
	owl_output *output;
	owl_screencopy_frame_state state;
	int32_t x, y, width, height;
	bool overlay_cursor;
	bool with_damage;
	owl_shm_buffer *buffer;
} owl_screencopy_frame;

#define OWL_MAX_SCREENCOPY_FRAMES 16
#define OWL_MAX_MIME_TYPES 32

typedef struct owl_data_source {
	owl_display *display;
	struct wl_resource *resource;
	char *mime_types[OWL_MAX_MIME_TYPES];
	int mime_type_count;
	uint32_t dnd_actions;
	uint32_t current_dnd_action;
	bool actions_set;
	struct wl_list offers;
} owl_data_source;

typedef struct owl_data_offer {
	owl_display *display;
	struct wl_resource *resource;
	owl_data_source *source;
	uint32_t dnd_actions;
	uint32_t preferred_dnd_action;
	bool in_ask;
	struct wl_list link;
} owl_data_offer;

typedef struct owl_primary_source {
	owl_display *display;
	struct wl_resource *resource;
	char *mime_types[OWL_MAX_MIME_TYPES];
	int mime_type_count;
	struct wl_list offers;
} owl_primary_source;

typedef struct owl_primary_offer {
	owl_display *display;
	struct wl_resource *resource;
	owl_primary_source *source;
	struct wl_list link;
} owl_primary_offer;

typedef struct owl_drag {
	owl_display *display;
	owl_data_source *source;
	owl_surface *origin;
	owl_surface *icon;
	owl_surface *focus;
	owl_data_offer *offer;
	uint32_t serial;
	double x, y;
	bool active;
} owl_drag;

typedef struct owl_session_lock {
	owl_display *display;
	struct wl_resource *resource;
	bool locked;
} owl_session_lock;

typedef struct owl_session_lock_surface {
	owl_display *display;
	owl_session_lock *lock;
	struct wl_resource *resource;
	owl_surface *surface;
	owl_output *output;
	uint32_t pending_serial;
	bool configured;
} owl_session_lock_surface;

#define OWL_MAX_LOCK_SURFACES 8

typedef struct owl_data_device {
	owl_display *display;
	struct wl_resource *resource;
	struct wl_client *client;
	struct wl_list link;
} owl_data_device;

typedef struct owl_primary_device {
	owl_display *display;
	struct wl_resource *resource;
	struct wl_client *client;
	struct wl_list link;
} owl_primary_device;

struct owl_display {
	struct wl_display *wayland_display;
	struct wl_event_loop *event_loop;
	const char *socket_name;
	bool running;

	int drm_fd;
	struct gbm_device *gbm_device;
	void *egl_display;
	void *egl_context;
	void *egl_config;

	struct libinput *libinput;
	struct udev *udev;
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;
	uint32_t modifier_state;
	int32_t keyboard_repeat_rate;
	int32_t keyboard_repeat_delay;

	owl_output *outputs[OWL_MAX_OUTPUTS];
	int output_count;

	struct wl_list windows;
	int window_count;

	struct wl_list surfaces;
	int surface_count;
	struct wl_global *compositor_global;
	struct wl_global *shm_global;
	struct wl_global *subcompositor_global;
	struct wl_global *data_device_manager_global;

	window_callback_entry window_callbacks[12][OWL_MAX_CALLBACKS];
	int window_callback_count[12];

	input_callback_entry input_callbacks[5][OWL_MAX_CALLBACKS];
	int input_callback_count[5];

	output_callback_entry output_callbacks[3][OWL_MAX_CALLBACKS];
	int output_callback_count[3];

	layer_surface_callback_entry layer_surface_callbacks[4][OWL_MAX_CALLBACKS];
	int layer_surface_callback_count[4];

	owl_layer_surface *layer_surfaces[OWL_MAX_LAYER_SURFACES];
	int layer_surface_count;
	struct wl_global *layer_shell_global;

	struct wl_list workspaces;
	int workspace_count;
	struct wl_global *workspace_manager_global;
	struct wl_list workspace_manager_resources;
	struct wl_list workspace_group_resources;

	workspace_callback_entry workspace_callbacks[3][OWL_MAX_CALLBACKS];
	int workspace_callback_count[3];

	gesture_callback_entry gesture_callbacks[3][OWL_MAX_CALLBACKS];
	int gesture_callback_count[3];

	struct wl_event_source *drm_event_source;
	struct wl_event_source *libinput_event_source;

	struct wl_global *seat_global;
	struct wl_list keyboards;
	struct wl_list pointers;
	owl_surface *keyboard_focus;
	owl_surface *pointer_focus;
	double pointer_x;
	double pointer_y;
	int keymap_fd;
	uint32_t keymap_size;

	owl_surface *cursor_surface;
	int32_t cursor_hotspot_x;
	int32_t cursor_hotspot_y;

	owl_render_callback render_callback;
	void *render_callback_data;

	struct wl_global *screencopy_manager_global;
	owl_screencopy_frame *screencopy_frames[OWL_MAX_SCREENCOPY_FRAMES];
	int screencopy_frame_count;

	struct wl_list data_devices;
	owl_data_source *clipboard_source;
	uint32_t clipboard_serial;

	struct wl_global *primary_selection_manager_global;
	struct wl_list primary_devices;
	owl_primary_source *primary_source;
	uint32_t primary_serial;

	owl_drag current_drag;

	struct wl_global *dmabuf_global;
	owl_drm_format dmabuf_formats[OWL_MAX_DRM_FORMATS];
	int dmabuf_format_count;
	bool dmabuf_import_supported;

	struct wl_global *viewporter_global;

	struct wl_global *session_lock_manager_global;
	owl_session_lock *session_lock;
	owl_session_lock_surface *lock_surfaces[OWL_MAX_LOCK_SURFACES];
	int lock_surface_count;
	bool locked;

	owl_render_target current_render_target;
};

/* Internal functions */
void owl_output_init(owl_display *display);
void owl_output_cleanup(owl_display *display);
void owl_output_render_frame(owl_output *output);

void owl_input_init(owl_display *display);
void owl_input_cleanup(owl_display *display);
void owl_input_process_events(owl_display *display);

void owl_render_init(owl_display *display);
void owl_render_cleanup(owl_display *display);
void owl_render_frame(owl_display *display, owl_output *output);

void owl_invoke_window_callback(owl_display *display, owl_window_event type, owl_window *window);
bool owl_invoke_input_callback(owl_display *display, owl_input_event type, owl_input *input);
void owl_invoke_output_callback(owl_display *display, owl_output_event type, owl_output *output);

void owl_surface_init(owl_display *display);
void owl_surface_cleanup(owl_display *display);
owl_surface *owl_surface_from_resource(struct wl_resource *resource);
void owl_surface_send_frame_done(owl_display *display, uint32_t time);

void owl_xdg_shell_init(owl_display *display);
void owl_xdg_shell_cleanup(owl_display *display);
void owl_xdg_toplevel_send_configure(owl_window *window, int width, int height);
void owl_xdg_toplevel_send_close(owl_window *window);
void owl_window_map(owl_window *window);

void owl_layer_shell_init(owl_display *display);
void owl_layer_shell_cleanup(owl_display *display);
void owl_invoke_layer_surface_callback(owl_display *display, owl_layer_surface_event type, owl_layer_surface *surface);
void owl_layer_surface_send_initial_configure(owl_layer_surface *ls);

void owl_seat_init(owl_display *display);
void owl_seat_cleanup(owl_display *display);
void owl_seat_set_keyboard_focus(owl_display *display, owl_surface *surface);
void owl_seat_set_pointer_focus(owl_display *display, owl_surface *surface, double x, double y);
void owl_seat_send_key(owl_display *display, uint32_t key, uint32_t state);
void owl_seat_send_modifiers(owl_display *display);
void owl_seat_send_pointer_motion(owl_display *display, double x, double y);
void owl_seat_send_pointer_button(owl_display *display, uint32_t button, uint32_t state);

uint32_t owl_render_upload_texture(owl_display *display, owl_surface *surface);
void owl_render_surface(owl_display *display, owl_surface *surface, int x, int y);

void owl_workspace_init(owl_display *display);
void owl_workspace_cleanup(owl_display *display);
void owl_invoke_workspace_callback(owl_display *display, owl_workspace_event type, owl_workspace *workspace);

void owl_decoration_init(owl_display *display);
void owl_decoration_cleanup(owl_display *display);

void owl_invoke_gesture_callback(owl_display *display, owl_gesture_event type, owl_gesture *gesture);

void owl_screencopy_init(owl_display *display);
void owl_screencopy_cleanup(owl_display *display);
void owl_screencopy_do_copy(owl_screencopy_frame *frame);

void owl_selection_init(owl_display *display);
void owl_selection_send_offer(owl_display *display, struct wl_client *client);
void owl_primary_selection_init(owl_display *display);
void owl_primary_selection_cleanup(owl_display *display);
void owl_primary_send_offer(owl_display *display, struct wl_client *client);
void owl_drag_start(
    owl_display *display,
    owl_data_source *source,
    owl_surface *origin,
    owl_surface *icon,
    uint32_t serial
);
void owl_drag_update(owl_display *display, double x, double y);
void owl_drag_drop(owl_display *display);
void owl_drag_cancel(owl_display *display);

void owl_dmabuf_init(owl_display *display);
void owl_dmabuf_cleanup(owl_display *display);
bool owl_dmabuf_import(owl_display *display, owl_dmabuf_buffer *buffer);
void owl_dmabuf_buffer_destroy(owl_dmabuf_buffer *buffer);

void owl_session_lock_init(owl_display *display);
void owl_session_lock_cleanup(owl_display *display);

#endif
