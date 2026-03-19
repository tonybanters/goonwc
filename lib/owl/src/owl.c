/*
 * owl.c - Open Wayland Library
 *
 * Single-file Wayland compositor implementation.
 * Handles all protocols: wl_compositor, wl_shm, xdg_shell, layer_shell,
 * wl_output, ext_workspace, xdg_decoration.
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

/* protocol headers */
#include "xdg-shell-protocol.h"
#include "xdg-shell-protocol.c"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.c"
#include "xdg-output-unstable-v1-protocol.h"
#include "xdg-output-unstable-v1-protocol.c"
#include "ext-workspace-v1-protocol.h"
#include "ext-workspace-v1-protocol.c"
#include "xdg-decoration-protocol.h"
#include "xdg-decoration-protocol.c"

/* ==========================================================================
 * Debug logging
 * ========================================================================== */

static FILE *debug_log = NULL;

static void debug(const char *fmt, ...) {
	if (!debug_log) debug_log = fopen("/tmp/owl.log", "w");
	if (debug_log) {
		va_list args;
		va_start(args, fmt);
		vfprintf(debug_log, fmt, args);
		va_end(args);
		fflush(debug_log);
	}
}

/* ==========================================================================
 * Callbacks
 * ========================================================================== */

void owl_set_window_callback(owl_display *display, owl_window_event type,
                             owl_window_callback callback, void *data) {
	if (!display || type < 0 || type > OWL_WINDOW_EVENT_REQUEST_RESIZE) return;

	int count = display->window_callback_count[type];
	if (count >= OWL_MAX_CALLBACKS) return;

	display->window_callbacks[type][count].callback = callback;
	display->window_callbacks[type][count].data = data;
	display->window_callback_count[type]++;
}

void owl_set_input_callback(owl_display *display, owl_input_event type,
                            owl_input_callback callback, void *data) {
	if (!display || type < 0 || type > OWL_INPUT_EVENT_POINTER_MOTION) return;

	int count = display->input_callback_count[type];
	if (count >= OWL_MAX_CALLBACKS) return;

	display->input_callbacks[type][count].callback = callback;
	display->input_callbacks[type][count].data = data;
	display->input_callback_count[type]++;
}

void owl_set_output_callback(owl_display *display, owl_output_event type,
                             owl_output_callback callback, void *data) {
	if (!display || type < 0 || type > OWL_OUTPUT_EVENT_MODE_CHANGE) return;

	int count = display->output_callback_count[type];
	if (count >= OWL_MAX_CALLBACKS) return;

	display->output_callbacks[type][count].callback = callback;
	display->output_callbacks[type][count].data = data;
	display->output_callback_count[type]++;
}

void owl_set_layer_surface_callback(owl_display *display, owl_layer_surface_event type,
                                    owl_layer_surface_callback callback, void *data) {
	if (!display || type < 0 || type > OWL_LAYER_SURFACE_EVENT_UNMAP) return;

	int count = display->layer_surface_callback_count[type];
	if (count >= OWL_MAX_CALLBACKS) return;

	display->layer_surface_callbacks[type][count].callback = callback;
	display->layer_surface_callbacks[type][count].data = data;
	display->layer_surface_callback_count[type]++;
}

void owl_set_workspace_callback(owl_display *display, owl_workspace_event type,
                                owl_workspace_callback callback, void *data) {
	if (!display || type < 0 || type >= 3) return;

	int count = display->workspace_callback_count[type];
	if (count >= OWL_MAX_CALLBACKS) return;

	display->workspace_callbacks[type][count].callback = callback;
	display->workspace_callbacks[type][count].data = data;
	display->workspace_callback_count[type]++;
}

void owl_set_gesture_callback(owl_display *display, owl_gesture_event type,
                              owl_gesture_callback callback, void *data) {
	if (!display || type < 0 || type > OWL_GESTURE_SWIPE_END) return;

	int count = display->gesture_callback_count[type];
	if (count >= OWL_MAX_CALLBACKS) return;

	display->gesture_callbacks[type][count].callback = callback;
	display->gesture_callbacks[type][count].data = data;
	display->gesture_callback_count[type]++;
}

void owl_invoke_window_callback(owl_display *display, owl_window_event type, owl_window *window) {
	if (!display || type < 0 || type > OWL_WINDOW_EVENT_REQUEST_RESIZE) return;

	int count = display->window_callback_count[type];
	for (int i = 0; i < count; i++) {
		window_callback_entry *entry = &display->window_callbacks[type][i];
		if (entry->callback) {
			entry->callback(display, window, entry->data);
		}
	}
}

bool owl_invoke_input_callback(owl_display *display, owl_input_event type, owl_input *input) {
	if (!display || type < 0 || type > OWL_INPUT_EVENT_POINTER_MOTION) return false;

	bool handled = false;
	int count = display->input_callback_count[type];
	for (int i = 0; i < count; i++) {
		input_callback_entry *entry = &display->input_callbacks[type][i];
		if (entry->callback && entry->callback(display, input, entry->data)) {
			handled = true;
		}
	}
	return handled;
}

void owl_invoke_output_callback(owl_display *display, owl_output_event type, owl_output *output) {
	if (!display || type < 0 || type > OWL_OUTPUT_EVENT_MODE_CHANGE) return;

	int count = display->output_callback_count[type];
	for (int i = 0; i < count; i++) {
		output_callback_entry *entry = &display->output_callbacks[type][i];
		if (entry->callback) {
			entry->callback(display, output, entry->data);
		}
	}
}

void owl_invoke_layer_surface_callback(owl_display *display, owl_layer_surface_event type,
                                       owl_layer_surface *surface) {
	if (!display || type < 0 || type > OWL_LAYER_SURFACE_EVENT_UNMAP) return;

	int count = display->layer_surface_callback_count[type];
	for (int i = 0; i < count; i++) {
		layer_surface_callback_entry *entry = &display->layer_surface_callbacks[type][i];
		if (entry->callback) {
			entry->callback(display, surface, entry->data);
		}
	}
}

void owl_invoke_workspace_callback(owl_display *display, owl_workspace_event type,
                                   owl_workspace *workspace) {
	if (!display || type < 0 || type >= 3) return;

	int count = display->workspace_callback_count[type];
	for (int i = 0; i < count; i++) {
		workspace_callback_entry *entry = &display->workspace_callbacks[type][i];
		if (entry->callback) {
			entry->callback(display, workspace, entry->data);
		}
	}
}

void owl_invoke_gesture_callback(owl_display *display, owl_gesture_event type,
                                 owl_gesture *gesture) {
	if (!display || type < 0 || type > OWL_GESTURE_SWIPE_END) return;

	int count = display->gesture_callback_count[type];
	for (int i = 0; i < count; i++) {
		gesture_callback_entry *entry = &display->gesture_callbacks[type][i];
		if (entry->callback) {
			entry->callback(display, gesture, entry->data);
		}
	}
}

/* ==========================================================================
 * Surface / SHM / Compositor protocol
 * ========================================================================== */

static void shm_pool_destroy_handler(struct wl_resource *resource) {
	owl_shm_pool *pool = wl_resource_get_user_data(resource);
	if (!pool) return;

	pool->ref_count--;
	if (pool->ref_count <= 0) {
		if (pool->data) munmap(pool->data, pool->size);
		if (pool->fd >= 0) close(pool->fd);
		free(pool);
	}
}

static void shm_buffer_destroy_handler(struct wl_resource *resource) {
	owl_shm_buffer *buffer = wl_resource_get_user_data(resource);
	if (!buffer) return;

	if (buffer->pool) {
		buffer->pool->ref_count--;
		if (buffer->pool->ref_count <= 0 && buffer->pool->resource == NULL) {
			if (buffer->pool->data) munmap(buffer->pool->data, buffer->pool->size);
			if (buffer->pool->fd >= 0) close(buffer->pool->fd);
			free(buffer->pool);
		}
	}
	free(buffer);
}

static void shm_buffer_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	wl_resource_destroy(resource);
}

static const struct wl_buffer_interface buffer_interface = {
	.destroy = shm_buffer_destroy,
};

static void shm_pool_create_buffer(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t id, int32_t offset, int32_t width, int32_t height,
                                   int32_t stride, uint32_t format) {
	owl_shm_pool *pool = wl_resource_get_user_data(resource);
	if (!pool) {
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "invalid pool");
		return;
	}

	if (offset < 0 || width <= 0 || height <= 0 || stride < width * 4) {
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE, "invalid buffer parameters");
		return;
	}

	if (offset + stride * height > pool->size) {
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE, "buffer extends past pool");
		return;
	}

	if (format != WL_SHM_FORMAT_ARGB8888 && format != WL_SHM_FORMAT_XRGB8888) {
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FORMAT, "unsupported format");
		return;
	}

	owl_shm_buffer *buffer = calloc(1, sizeof(owl_shm_buffer));
	if (!buffer) {
		wl_resource_post_no_memory(resource);
		return;
	}

	buffer->pool = pool;
	buffer->offset = offset;
	buffer->width = width;
	buffer->height = height;
	buffer->stride = stride;
	buffer->format = format;
	buffer->busy = false;

	pool->ref_count++;

	buffer->resource = wl_resource_create(client, &wl_buffer_interface, 1, id);
	if (!buffer->resource) {
		pool->ref_count--;
		free(buffer);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(buffer->resource, &buffer_interface, buffer, shm_buffer_destroy_handler);
}

static void shm_pool_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	owl_shm_pool *pool = wl_resource_get_user_data(resource);
	if (pool) pool->resource = NULL;
	wl_resource_destroy(resource);
}

static void shm_pool_resize(struct wl_client *client, struct wl_resource *resource, int32_t size) {
	(void)client;
	owl_shm_pool *pool = wl_resource_get_user_data(resource);
	if (!pool) return;

	if (size < pool->size) {
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "cannot shrink pool");
		return;
	}

	void *new_data = mremap(pool->data, pool->size, size, MREMAP_MAYMOVE);
	if (new_data == MAP_FAILED) {
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "failed to resize pool");
		return;
	}

	pool->data = new_data;
	pool->size = size;
}

static const struct wl_shm_pool_interface shm_pool_interface = {
	.create_buffer = shm_pool_create_buffer,
	.destroy = shm_pool_destroy,
	.resize = shm_pool_resize,
};

static void shm_create_pool(struct wl_client *client, struct wl_resource *resource,
                            uint32_t id, int32_t fd, int32_t size) {
	owl_display *display = wl_resource_get_user_data(resource);

	if (size <= 0) {
		close(fd);
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "invalid pool size");
		return;
	}

	owl_shm_pool *pool = calloc(1, sizeof(owl_shm_pool));
	if (!pool) {
		close(fd);
		wl_resource_post_no_memory(resource);
		return;
	}

	pool->display = display;
	pool->fd = fd;
	pool->size = size;
	pool->ref_count = 1;

	pool->data = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (pool->data == MAP_FAILED) {
		close(fd);
		free(pool);
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "failed to mmap pool");
		return;
	}

	pool->resource = wl_resource_create(client, &wl_shm_pool_interface, 1, id);
	if (!pool->resource) {
		munmap(pool->data, size);
		close(fd);
		free(pool);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(pool->resource, &shm_pool_interface, pool, shm_pool_destroy_handler);
}

static const struct wl_shm_interface shm_interface = {
	.create_pool = shm_create_pool,
};

static void shm_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	owl_display *display = data;
	(void)version;

	struct wl_resource *resource = wl_resource_create(client, &wl_shm_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &shm_interface, display, NULL);
	wl_shm_send_format(resource, WL_SHM_FORMAT_ARGB8888);
	wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
}

/* surface state helpers */
static void surface_state_init(owl_surface_state *state) {
	memset(state, 0, sizeof(owl_surface_state));
	wl_list_init(&state->frame_callbacks);
}

static void surface_state_cleanup(owl_surface_state *state) {
	owl_frame_callback *callback, *tmp;
	wl_list_for_each_safe(callback, tmp, &state->frame_callbacks, link) {
		wl_list_remove(&callback->link);
		free(callback);
	}
}

static void surface_destroy_handler(struct wl_resource *resource) {
	owl_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;

	if (surface->display->cursor_surface == surface)
		surface->display->cursor_surface = NULL;
	if (surface->display->keyboard_focus == surface)
		surface->display->keyboard_focus = NULL;
	if (surface->display->pointer_focus == surface)
		surface->display->pointer_focus = NULL;

	wl_list_remove(&surface->link);
	surface->display->surface_count--;

	surface_state_cleanup(&surface->pending);
	surface_state_cleanup(&surface->current);
	free(surface);
}

static void surface_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client, struct wl_resource *resource,
                           struct wl_resource *buffer_resource, int32_t x, int32_t y) {
	(void)client;
	owl_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;

	surface->pending.buffer = buffer_resource ? wl_resource_get_user_data(buffer_resource) : NULL;
	surface->pending.buffer_x = x;
	surface->pending.buffer_y = y;
	surface->pending.buffer_attached = true;
}

static void surface_damage(struct wl_client *client, struct wl_resource *resource,
                           int32_t x, int32_t y, int32_t width, int32_t height) {
	(void)client;
	owl_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;

	surface->pending.damage_x = x;
	surface->pending.damage_y = y;
	surface->pending.damage_width = width;
	surface->pending.damage_height = height;
	surface->pending.has_damage = true;
}

static void surface_frame(struct wl_client *client, struct wl_resource *resource, uint32_t callback_id) {
	owl_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;

	owl_frame_callback *callback = calloc(1, sizeof(owl_frame_callback));
	if (!callback) {
		wl_resource_post_no_memory(resource);
		return;
	}

	callback->resource = wl_resource_create(client, &wl_callback_interface, 1, callback_id);
	if (!callback->resource) {
		free(callback);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(callback->resource, NULL, callback, NULL);
	wl_list_insert(surface->pending.frame_callbacks.prev, &callback->link);
}

static void surface_set_opaque_region(struct wl_client *client, struct wl_resource *resource,
                                      struct wl_resource *region) {
	(void)client; (void)resource; (void)region;
}

static void surface_set_input_region(struct wl_client *client, struct wl_resource *resource,
                                     struct wl_resource *region) {
	(void)client; (void)resource; (void)region;
}

static owl_window *find_window_for_surface(owl_display *display, owl_surface *surface) {
	owl_window *window;
	wl_list_for_each(window, &display->windows, link) {
		if (window->surface == surface) return window;
	}
	return NULL;
}

static owl_layer_surface *find_layer_surface_for_surface(owl_display *display, owl_surface *surface) {
	for (int i = 0; i < display->layer_surface_count; i++) {
		owl_layer_surface *ls = display->layer_surfaces[i];
		if (ls->surface == surface) return ls;
	}
	return NULL;
}

static void surface_commit(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	owl_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;

	if (surface->pending.buffer_attached) {
		surface->current.buffer = surface->pending.buffer;
		surface->current.buffer_x = surface->pending.buffer_x;
		surface->current.buffer_y = surface->pending.buffer_y;
		surface->pending.buffer_attached = false;
	}

	if (surface->pending.has_damage) {
		surface->current.damage_x = surface->pending.damage_x;
		surface->current.damage_y = surface->pending.damage_y;
		surface->current.damage_width = surface->pending.damage_width;
		surface->current.damage_height = surface->pending.damage_height;
		surface->current.has_damage = true;
		surface->pending.has_damage = false;
	}

	wl_list_insert_list(&surface->current.frame_callbacks, &surface->pending.frame_callbacks);
	wl_list_init(&surface->pending.frame_callbacks);

	/* send initial configure to layer surfaces */
	owl_layer_surface *ls = find_layer_surface_for_surface(surface->display, surface);
	if (ls) owl_layer_surface_send_initial_configure(ls);

	if (surface->current.buffer) {
		owl_render_upload_texture(surface->display, surface);
		surface->has_content = true;

		owl_window *window = find_window_for_surface(surface->display, surface);
		if (window && window->xdg_toplevel_resource && !window->mapped) {
			if (window->width == 0 && window->height == 0) {
				window->width = surface->texture_width;
				window->height = surface->texture_height;
			}
			owl_window_map(window);
		}

		owl_layer_surface *layer_surface = find_layer_surface_for_surface(surface->display, surface);
		if (layer_surface && !layer_surface->mapped) {
			layer_surface->mapped = true;
			owl_invoke_layer_surface_callback(surface->display, OWL_LAYER_SURFACE_EVENT_MAP, layer_surface);
			if (layer_surface->keyboard_interactivity == OWL_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
				owl_seat_set_keyboard_focus(surface->display, surface);
			}
		}

		for (int i = 0; i < surface->display->output_count; i++) {
			owl_render_frame(surface->display, surface->display->outputs[i]);
		}
	}
}

static void surface_set_buffer_transform(struct wl_client *client, struct wl_resource *resource, int32_t transform) {
	(void)client; (void)resource; (void)transform;
}

static void surface_set_buffer_scale(struct wl_client *client, struct wl_resource *resource, int32_t scale) {
	(void)client; (void)resource; (void)scale;
}

static void surface_damage_buffer(struct wl_client *client, struct wl_resource *resource,
                                  int32_t x, int32_t y, int32_t width, int32_t height) {
	surface_damage(client, resource, x, y, width, height);
}

static void surface_offset(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y) {
	(void)client; (void)resource; (void)x; (void)y;
}

static const struct wl_surface_interface surface_interface = {
	.destroy = surface_destroy,
	.attach = surface_attach,
	.damage = surface_damage,
	.frame = surface_frame,
	.set_opaque_region = surface_set_opaque_region,
	.set_input_region = surface_set_input_region,
	.commit = surface_commit,
	.set_buffer_transform = surface_set_buffer_transform,
	.set_buffer_scale = surface_set_buffer_scale,
	.damage_buffer = surface_damage_buffer,
	.offset = surface_offset,
};

static void compositor_create_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
	owl_display *display = wl_resource_get_user_data(resource);

	owl_surface *surface = calloc(1, sizeof(owl_surface));
	if (!surface) {
		wl_resource_post_no_memory(resource);
		return;
	}

	surface->display = display;
	surface_state_init(&surface->pending);
	surface_state_init(&surface->current);

	uint32_t version = wl_resource_get_version(resource);
	surface->resource = wl_resource_create(client, &wl_surface_interface, version, id);
	if (!surface->resource) {
		free(surface);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(surface->resource, &surface_interface, surface, surface_destroy_handler);
	wl_list_insert(&display->surfaces, &surface->link);
	display->surface_count++;
}

static void region_destroy_handler(struct wl_resource *resource) { (void)resource; }

static void region_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	wl_resource_destroy(resource);
}

static void region_add(struct wl_client *client, struct wl_resource *resource,
                       int32_t x, int32_t y, int32_t width, int32_t height) {
	(void)client; (void)resource; (void)x; (void)y; (void)width; (void)height;
}

static void region_subtract(struct wl_client *client, struct wl_resource *resource,
                            int32_t x, int32_t y, int32_t width, int32_t height) {
	(void)client; (void)resource; (void)x; (void)y; (void)width; (void)height;
}

static const struct wl_region_interface region_interface = {
	.destroy = region_destroy,
	.add = region_add,
	.subtract = region_subtract,
};

static void compositor_create_region(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
	struct wl_resource *region_resource = wl_resource_create(client, &wl_region_interface, 1, id);
	if (!region_resource) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(region_resource, &region_interface, NULL, region_destroy_handler);
}

static const struct wl_compositor_interface compositor_interface = {
	.create_surface = compositor_create_surface,
	.create_region = compositor_create_region,
};

static void compositor_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	owl_display *display = data;
	uint32_t bound_version = version < 6 ? version : 6;

	struct wl_resource *resource = wl_resource_create(client, &wl_compositor_interface, bound_version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &compositor_interface, display, NULL);
}

/* subcompositor stubs */
static void subsurface_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}
static void subsurface_set_position(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y) {
	(void)client; (void)resource; (void)x; (void)y;
}
static void subsurface_place_above(struct wl_client *client, struct wl_resource *resource, struct wl_resource *sibling) {
	(void)client; (void)resource; (void)sibling;
}
static void subsurface_place_below(struct wl_client *client, struct wl_resource *resource, struct wl_resource *sibling) {
	(void)client; (void)resource; (void)sibling;
}
static void subsurface_set_sync(struct wl_client *client, struct wl_resource *resource) {
	(void)client; (void)resource;
}
static void subsurface_set_desync(struct wl_client *client, struct wl_resource *resource) {
	(void)client; (void)resource;
}

static const struct wl_subsurface_interface subsurface_interface = {
	.destroy = subsurface_destroy,
	.set_position = subsurface_set_position,
	.place_above = subsurface_place_above,
	.place_below = subsurface_place_below,
	.set_sync = subsurface_set_sync,
	.set_desync = subsurface_set_desync,
};

static void subcompositor_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void subcompositor_get_subsurface(struct wl_client *client, struct wl_resource *resource,
                                         uint32_t id, struct wl_resource *surface_resource,
                                         struct wl_resource *parent_resource) {
	(void)surface_resource; (void)parent_resource;

	struct wl_resource *subsurface_resource = wl_resource_create(client, &wl_subsurface_interface, 1, id);
	if (!subsurface_resource) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(subsurface_resource, &subsurface_interface, NULL, NULL);
}

static const struct wl_subcompositor_interface subcompositor_interface = {
	.destroy = subcompositor_destroy,
	.get_subsurface = subcompositor_get_subsurface,
};

static void subcompositor_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	(void)data; (void)version;

	struct wl_resource *resource = wl_resource_create(client, &wl_subcompositor_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &subcompositor_interface, NULL, NULL);
}

/* data device stubs */
static void data_offer_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}
static void data_offer_accept(struct wl_client *client, struct wl_resource *resource,
                              uint32_t serial, const char *mime_type) {
	(void)client; (void)resource; (void)serial; (void)mime_type;
}
static void data_offer_receive(struct wl_client *client, struct wl_resource *resource,
                               const char *mime_type, int32_t fd) {
	(void)client; (void)resource; (void)mime_type; close(fd);
}
static void data_offer_finish(struct wl_client *client, struct wl_resource *resource) {
	(void)client; (void)resource;
}
static void data_offer_set_actions(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t dnd_actions, uint32_t preferred_action) {
	(void)client; (void)resource; (void)dnd_actions; (void)preferred_action;
}

static const struct wl_data_offer_interface data_offer_interface = {
	.accept = data_offer_accept,
	.receive = data_offer_receive,
	.destroy = data_offer_destroy,
	.finish = data_offer_finish,
	.set_actions = data_offer_set_actions,
};

static void data_source_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}
static void data_source_offer(struct wl_client *client, struct wl_resource *resource, const char *mime_type) {
	(void)client; (void)resource; (void)mime_type;
}
static void data_source_set_actions(struct wl_client *client, struct wl_resource *resource, uint32_t dnd_actions) {
	(void)client; (void)resource; (void)dnd_actions;
}

static const struct wl_data_source_interface data_source_interface = {
	.offer = data_source_offer,
	.destroy = data_source_destroy,
	.set_actions = data_source_set_actions,
};

static void data_device_start_drag(struct wl_client *client, struct wl_resource *resource,
                                   struct wl_resource *source, struct wl_resource *origin,
                                   struct wl_resource *icon, uint32_t serial) {
	(void)client; (void)resource; (void)source; (void)origin; (void)icon; (void)serial;
}
static void data_device_set_selection(struct wl_client *client, struct wl_resource *resource,
                                      struct wl_resource *source, uint32_t serial) {
	(void)client; (void)resource; (void)source; (void)serial;
}
static void data_device_release(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static const struct wl_data_device_interface data_device_interface = {
	.start_drag = data_device_start_drag,
	.set_selection = data_device_set_selection,
	.release = data_device_release,
};

static void data_device_manager_create_data_source(struct wl_client *client,
                                                   struct wl_resource *resource, uint32_t id) {
	struct wl_resource *source = wl_resource_create(client, &wl_data_source_interface, 3, id);
	if (!source) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(source, &data_source_interface, NULL, NULL);
}

static void data_device_manager_get_data_device(struct wl_client *client,
                                                struct wl_resource *resource,
                                                uint32_t id, struct wl_resource *seat) {
	(void)seat;
	struct wl_resource *device = wl_resource_create(client, &wl_data_device_interface, 3, id);
	if (!device) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(device, &data_device_interface, NULL, NULL);
}

static const struct wl_data_device_manager_interface data_device_manager_interface = {
	.create_data_source = data_device_manager_create_data_source,
	.get_data_device = data_device_manager_get_data_device,
};

static void data_device_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	(void)data;
	uint32_t bound_version = version < 3 ? version : 3;
	struct wl_resource *resource = wl_resource_create(client, &wl_data_device_manager_interface,
	                                                  bound_version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &data_device_manager_interface, NULL, NULL);
}

void owl_surface_init(owl_display *display) {
	wl_list_init(&display->surfaces);

	display->compositor_global = wl_global_create(display->wayland_display,
		&wl_compositor_interface, 6, display, compositor_bind);

	display->shm_global = wl_global_create(display->wayland_display,
		&wl_shm_interface, 1, display, shm_bind);

	display->subcompositor_global = wl_global_create(display->wayland_display,
		&wl_subcompositor_interface, 1, display, subcompositor_bind);

	display->data_device_manager_global = wl_global_create(display->wayland_display,
		&wl_data_device_manager_interface, 3, display, data_device_manager_bind);

	fprintf(stderr, "owl: surface protocol initialized\n");
}

void owl_surface_cleanup(owl_display *display) {
	owl_surface *surface, *tmp;
	wl_list_for_each_safe(surface, tmp, &display->surfaces, link) {
		wl_resource_destroy(surface->resource);
	}

	if (display->data_device_manager_global) {
		wl_global_destroy(display->data_device_manager_global);
		display->data_device_manager_global = NULL;
	}
	if (display->subcompositor_global) {
		wl_global_destroy(display->subcompositor_global);
		display->subcompositor_global = NULL;
	}
	if (display->shm_global) {
		wl_global_destroy(display->shm_global);
		display->shm_global = NULL;
	}
	if (display->compositor_global) {
		wl_global_destroy(display->compositor_global);
		display->compositor_global = NULL;
	}
}

owl_surface *owl_surface_from_resource(struct wl_resource *resource) {
	if (!resource) return NULL;
	return wl_resource_get_user_data(resource);
}

void owl_surface_send_frame_done(owl_display *display, uint32_t time) {
	owl_surface *surface;
	wl_list_for_each(surface, &display->surfaces, link) {
		owl_frame_callback *callback, *tmp;
		wl_list_for_each_safe(callback, tmp, &surface->current.frame_callbacks, link) {
			wl_callback_send_done(callback->resource, time);
			wl_resource_destroy(callback->resource);
			wl_list_remove(&callback->link);
			free(callback);
		}
	}
}

owl_window **owl_display_get_windows(owl_display *display, int *count) {
	if (!display || !count) {
		if (count) *count = 0;
		return NULL;
	}

	*count = display->window_count;
	if (display->window_count == 0) return NULL;

	static owl_window *window_array[OWL_MAX_WINDOWS];
	int index = 0;

	owl_window *window;
	wl_list_for_each(window, &display->windows, link) {
		if (index < OWL_MAX_WINDOWS && window->mapped) {
			window_array[index++] = window;
		}
	}

	*count = index;
	return window_array;
}

void owl_window_focus(owl_window *window) {
	if (!window) return;

	owl_window *other;
	wl_list_for_each(other, &window->display->windows, link) {
		if (other->focused && other != window) {
			other->focused = false;
			owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_UNFOCUS, other);
			owl_xdg_toplevel_send_configure(other, other->width, other->height);
		}
	}

	window->focused = true;
	owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_FOCUS, window);
	owl_xdg_toplevel_send_configure(window, window->width, window->height);

	if (window->surface) {
		owl_seat_set_keyboard_focus(window->display, window->surface);
	}
}

void owl_window_move(owl_window *window, int new_x, int new_y) {
	if (!window) return;
	window->x = new_x;
	window->y = new_y;
}

void owl_window_resize(owl_window *window, int width, int height) {
	if (!window) return;
	owl_xdg_toplevel_send_configure(window, width, height);
}

void owl_window_close(owl_window *window) {
	if (!window) return;
	owl_xdg_toplevel_send_close(window);
}

void owl_window_set_fullscreen(owl_window *window, bool fullscreen) {
	if (!window) return;
	window->fullscreen = fullscreen;
	owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_FULLSCREEN, window);
}

void owl_window_set_tiled(owl_window *window, bool tiled) {
	if (!window) return;
	window->tiled = tiled;
}

/* ==========================================================================
 * XDG Shell protocol
 * ========================================================================== */

static struct wl_global *xdg_wm_base_global = NULL;

static void xdg_toplevel_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void xdg_toplevel_set_parent(struct wl_client *client, struct wl_resource *resource,
                                    struct wl_resource *parent) {
	(void)client; (void)resource; (void)parent;
}

static void xdg_toplevel_set_title(struct wl_client *client, struct wl_resource *resource,
                                   const char *title) {
	(void)client;
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;

	free(window->title);
	window->title = title ? strdup(title) : NULL;
	owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_TITLE_CHANGE, window);
}

static void xdg_toplevel_set_app_id(struct wl_client *client, struct wl_resource *resource,
                                    const char *app_id) {
	(void)client;
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;

	free(window->app_id);
	window->app_id = app_id ? strdup(app_id) : NULL;
}

static void xdg_toplevel_show_window_menu(struct wl_client *client, struct wl_resource *resource,
                                          struct wl_resource *seat, uint32_t serial,
                                          int32_t x, int32_t y) {
	(void)client; (void)resource; (void)seat; (void)serial; (void)x; (void)y;
}

static void xdg_toplevel_move(struct wl_client *client, struct wl_resource *resource,
                              struct wl_resource *seat, uint32_t serial) {
	(void)client;
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;
	(void)seat; (void)serial;
	owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_REQUEST_MOVE, window);
}

static void xdg_toplevel_resize(struct wl_client *client, struct wl_resource *resource,
                                struct wl_resource *seat, uint32_t serial, uint32_t edges) {
	(void)client;
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;
	(void)seat; (void)serial; (void)edges;
	owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_REQUEST_RESIZE, window);
}

static void xdg_toplevel_set_max_size(struct wl_client *client, struct wl_resource *resource,
                                      int32_t width, int32_t height) {
	(void)client; (void)resource; (void)width; (void)height;
}

static void xdg_toplevel_set_min_size(struct wl_client *client, struct wl_resource *resource,
                                      int32_t width, int32_t height) {
	(void)client; (void)resource; (void)width; (void)height;
}

static void xdg_toplevel_set_maximized(struct wl_client *client, struct wl_resource *resource) {
	(void)client; (void)resource;
}

static void xdg_toplevel_unset_maximized(struct wl_client *client, struct wl_resource *resource) {
	(void)client; (void)resource;
}

static void xdg_toplevel_set_fullscreen(struct wl_client *client, struct wl_resource *resource,
                                        struct wl_resource *output) {
	(void)client; (void)output;
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;

	window->fullscreen = true;
	owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_FULLSCREEN, window);
}

static void xdg_toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;

	window->fullscreen = false;
	owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_FULLSCREEN, window);
}

static void xdg_toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource) {
	(void)client; (void)resource;
}

static const struct xdg_toplevel_interface toplevel_interface = {
	.destroy = xdg_toplevel_destroy,
	.set_parent = xdg_toplevel_set_parent,
	.set_title = xdg_toplevel_set_title,
	.set_app_id = xdg_toplevel_set_app_id,
	.show_window_menu = xdg_toplevel_show_window_menu,
	.move = xdg_toplevel_move,
	.resize = xdg_toplevel_resize,
	.set_max_size = xdg_toplevel_set_max_size,
	.set_min_size = xdg_toplevel_set_min_size,
	.set_maximized = xdg_toplevel_set_maximized,
	.unset_maximized = xdg_toplevel_unset_maximized,
	.set_fullscreen = xdg_toplevel_set_fullscreen,
	.unset_fullscreen = xdg_toplevel_unset_fullscreen,
	.set_minimized = xdg_toplevel_set_minimized,
};

static void xdg_toplevel_destroy_handler(struct wl_resource *resource) {
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;
	window->xdg_toplevel_resource = NULL;
}

static void send_toplevel_configure(owl_window *window) {
	struct wl_array states;
	wl_array_init(&states);

	if (window->focused) {
		uint32_t *state = wl_array_add(&states, sizeof(uint32_t));
		*state = XDG_TOPLEVEL_STATE_ACTIVATED;
	}

	if (window->fullscreen) {
		uint32_t *state = wl_array_add(&states, sizeof(uint32_t));
		*state = XDG_TOPLEVEL_STATE_FULLSCREEN;
	}

	if (window->tiled) {
		uint32_t *s;
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = XDG_TOPLEVEL_STATE_TILED_LEFT;
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = XDG_TOPLEVEL_STATE_TILED_RIGHT;
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = XDG_TOPLEVEL_STATE_TILED_TOP;
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = XDG_TOPLEVEL_STATE_TILED_BOTTOM;
	}

	xdg_toplevel_send_configure(window->xdg_toplevel_resource,
	                            window->width, window->height, &states);
	wl_array_release(&states);
}

static void xdg_surface_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void xdg_surface_get_toplevel(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;

	uint32_t version = wl_resource_get_version(resource);
	window->xdg_toplevel_resource = wl_resource_create(client, &xdg_toplevel_interface, version, id);
	if (!window->xdg_toplevel_resource) {
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(window->xdg_toplevel_resource, &toplevel_interface,
	                               window, xdg_toplevel_destroy_handler);

	send_toplevel_configure(window);
	static uint32_t configure_serial = 1;
	window->pending_serial = configure_serial;
	window->pending_configure = true;
	xdg_surface_send_configure(window->xdg_surface_resource, configure_serial++);
}

/* popup stubs */
static void xdg_popup_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}
static void xdg_popup_grab(struct wl_client *client, struct wl_resource *resource,
                           struct wl_resource *seat, uint32_t serial) {
	(void)client; (void)resource; (void)seat; (void)serial;
}
static void xdg_popup_reposition(struct wl_client *client, struct wl_resource *resource,
                                 struct wl_resource *positioner, uint32_t token) {
	(void)client; (void)resource; (void)positioner; (void)token;
}

static const struct xdg_popup_interface popup_interface = {
	.destroy = xdg_popup_destroy,
	.grab = xdg_popup_grab,
	.reposition = xdg_popup_reposition,
};

static void xdg_surface_get_popup(struct wl_client *client, struct wl_resource *resource,
                                  uint32_t id, struct wl_resource *parent,
                                  struct wl_resource *positioner) {
	(void)parent; (void)positioner;

	uint32_t version = wl_resource_get_version(resource);
	struct wl_resource *popup = wl_resource_create(client, &xdg_popup_interface, version, id);
	if (!popup) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(popup, &popup_interface, NULL, NULL);

	xdg_popup_send_configure(popup, 0, 0, 100, 100);
	xdg_surface_send_configure(resource, 1);
}

static void xdg_surface_set_window_geometry(struct wl_client *client, struct wl_resource *resource,
                                            int32_t x, int32_t y, int32_t width, int32_t height) {
	(void)client;
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;

	window->geometry_x = x;
	window->geometry_y = y;
	window->geometry_width = width;
	window->geometry_height = height;
}

static void xdg_surface_ack_configure(struct wl_client *client, struct wl_resource *resource,
                                      uint32_t serial) {
	(void)client;
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;

	if (window->pending_serial == serial) {
		window->pending_configure = false;
	}
}

static const struct xdg_surface_interface xdg_surf_interface = {
	.destroy = xdg_surface_destroy,
	.get_toplevel = xdg_surface_get_toplevel,
	.get_popup = xdg_surface_get_popup,
	.set_window_geometry = xdg_surface_set_window_geometry,
	.ack_configure = xdg_surface_ack_configure,
};

static void xdg_surface_destroy_handler(struct wl_resource *resource) {
	owl_window *window = wl_resource_get_user_data(resource);
	if (!window) return;

	if (window->mapped) {
		window->mapped = false;
		owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_DESTROY, window);
	}

	wl_list_remove(&window->link);
	window->display->window_count--;

	free(window->title);
	free(window->app_id);
	free(window);
}

/* positioner stubs */
static void xdg_positioner_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}
static void xdg_positioner_set_size(struct wl_client *client, struct wl_resource *resource,
                                    int32_t width, int32_t height) {
	(void)client; (void)resource; (void)width; (void)height;
}
static void xdg_positioner_set_anchor_rect(struct wl_client *client, struct wl_resource *resource,
                                           int32_t x, int32_t y, int32_t width, int32_t height) {
	(void)client; (void)resource; (void)x; (void)y; (void)width; (void)height;
}
static void xdg_positioner_set_anchor(struct wl_client *client, struct wl_resource *resource, uint32_t anchor) {
	(void)client; (void)resource; (void)anchor;
}
static void xdg_positioner_set_gravity(struct wl_client *client, struct wl_resource *resource, uint32_t gravity) {
	(void)client; (void)resource; (void)gravity;
}
static void xdg_positioner_set_constraint_adjustment(struct wl_client *client, struct wl_resource *resource,
                                                     uint32_t constraint_adjustment) {
	(void)client; (void)resource; (void)constraint_adjustment;
}
static void xdg_positioner_set_offset(struct wl_client *client, struct wl_resource *resource,
                                      int32_t x, int32_t y) {
	(void)client; (void)resource; (void)x; (void)y;
}
static void xdg_positioner_set_reactive(struct wl_client *client, struct wl_resource *resource) {
	(void)client; (void)resource;
}
static void xdg_positioner_set_parent_size(struct wl_client *client, struct wl_resource *resource,
                                           int32_t parent_width, int32_t parent_height) {
	(void)client; (void)resource; (void)parent_width; (void)parent_height;
}
static void xdg_positioner_set_parent_configure(struct wl_client *client, struct wl_resource *resource,
                                                uint32_t serial) {
	(void)client; (void)resource; (void)serial;
}

static const struct xdg_positioner_interface positioner_interface = {
	.destroy = xdg_positioner_destroy,
	.set_size = xdg_positioner_set_size,
	.set_anchor_rect = xdg_positioner_set_anchor_rect,
	.set_anchor = xdg_positioner_set_anchor,
	.set_gravity = xdg_positioner_set_gravity,
	.set_constraint_adjustment = xdg_positioner_set_constraint_adjustment,
	.set_offset = xdg_positioner_set_offset,
	.set_reactive = xdg_positioner_set_reactive,
	.set_parent_size = xdg_positioner_set_parent_size,
	.set_parent_configure = xdg_positioner_set_parent_configure,
};

static void xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void xdg_wm_base_create_positioner(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
	uint32_t version = wl_resource_get_version(resource);
	struct wl_resource *positioner = wl_resource_create(client, &xdg_positioner_interface, version, id);
	if (!positioner) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(positioner, &positioner_interface, NULL, NULL);
}

static void xdg_wm_base_get_xdg_surface(struct wl_client *client, struct wl_resource *resource,
                                        uint32_t id, struct wl_resource *surface_resource) {
	owl_display *display = wl_resource_get_user_data(resource);
	owl_surface *surface = owl_surface_from_resource(surface_resource);

	if (!surface) {
		wl_resource_post_error(resource, XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE, "surface is null");
		return;
	}

	owl_window *window = calloc(1, sizeof(owl_window));
	if (!window) {
		wl_resource_post_no_memory(resource);
		return;
	}

	window->display = display;
	window->surface = surface;
	window->width = 0;
	window->height = 0;

	uint32_t version = wl_resource_get_version(resource);
	window->xdg_surface_resource = wl_resource_create(client, &xdg_surface_interface, version, id);
	if (!window->xdg_surface_resource) {
		free(window);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(window->xdg_surface_resource, &xdg_surf_interface,
	                               window, xdg_surface_destroy_handler);

	wl_list_insert(&display->windows, &window->link);
	display->window_count++;
}

static void xdg_wm_base_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial) {
	(void)client; (void)resource; (void)serial;
}

static const struct xdg_wm_base_interface wm_base_interface = {
	.destroy = xdg_wm_base_destroy,
	.create_positioner = xdg_wm_base_create_positioner,
	.get_xdg_surface = xdg_wm_base_get_xdg_surface,
	.pong = xdg_wm_base_pong,
};

static void wm_base_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	owl_display *display = data;
	uint32_t bound_version = version < 3 ? version : 3;

	struct wl_resource *resource = wl_resource_create(client, &xdg_wm_base_interface, bound_version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &wm_base_interface, display, NULL);
}

void owl_xdg_shell_init(owl_display *display) {
	xdg_wm_base_global = wl_global_create(display->wayland_display,
		&xdg_wm_base_interface, 3, display, wm_base_bind);

	if (!xdg_wm_base_global) {
		fprintf(stderr, "owl: failed to create xdg_wm_base global\n");
		return;
	}
	fprintf(stderr, "owl: xdg-shell initialized\n");
}

void owl_xdg_shell_cleanup(owl_display *display) {
	(void)display;
	if (xdg_wm_base_global) {
		wl_global_destroy(xdg_wm_base_global);
		xdg_wm_base_global = NULL;
	}
}

void owl_xdg_toplevel_send_configure(owl_window *window, int width, int height) {
	if (!window || !window->xdg_toplevel_resource || !window->xdg_surface_resource) return;

	window->width = width;
	window->height = height;

	send_toplevel_configure(window);

	static uint32_t serial = 1;
	window->pending_serial = serial;
	window->pending_configure = true;
	xdg_surface_send_configure(window->xdg_surface_resource, serial++);
}

void owl_xdg_toplevel_send_close(owl_window *window) {
	if (!window || !window->xdg_toplevel_resource) return;
	xdg_toplevel_send_close(window->xdg_toplevel_resource);
}

void owl_window_map(owl_window *window) {
	if (!window || window->mapped) return;

	window->mapped = true;
	owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_CREATE, window);

	for (int i = 0; i < window->display->output_count; i++) {
		owl_render_frame(window->display, window->display->outputs[i]);
	}
}

/* ==========================================================================
 * Layer Shell protocol
 * ========================================================================== */

static uint32_t layer_surface_configure_serial = 1;

static void layer_surface_set_size(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t width, uint32_t height) {
	(void)client;
	owl_layer_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;
	surface->width = width;
	surface->height = height;
}

static void layer_surface_set_anchor(struct wl_client *client, struct wl_resource *resource, uint32_t anchor) {
	(void)client;
	owl_layer_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;
	surface->anchor = anchor;
}

static void layer_surface_set_exclusive_zone(struct wl_client *client, struct wl_resource *resource, int32_t zone) {
	(void)client;
	owl_layer_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;
	surface->exclusive_zone = zone;
}

static void layer_surface_set_margin(struct wl_client *client, struct wl_resource *resource,
                                     int32_t top, int32_t right, int32_t bottom, int32_t left) {
	(void)client;
	owl_layer_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;
	surface->margin_top = top;
	surface->margin_right = right;
	surface->margin_bottom = bottom;
	surface->margin_left = left;
}

static void layer_surface_set_keyboard_interactivity(struct wl_client *client, struct wl_resource *resource,
                                                     uint32_t keyboard_interactivity) {
	(void)client;
	owl_layer_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;
	surface->keyboard_interactivity = keyboard_interactivity;
}

static void layer_surface_get_popup(struct wl_client *client, struct wl_resource *resource,
                                    struct wl_resource *popup) {
	(void)client; (void)resource; (void)popup;
}

static void layer_surface_ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial) {
	(void)client; (void)serial;
	owl_layer_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;
}

static void layer_surface_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void layer_surface_set_layer(struct wl_client *client, struct wl_resource *resource, uint32_t layer) {
	(void)client;
	owl_layer_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;
	surface->layer = layer;
}

static void layer_surface_set_exclusive_edge(struct wl_client *client, struct wl_resource *resource, uint32_t edge) {
	(void)client; (void)resource; (void)edge;
}

static const struct zwlr_layer_surface_v1_interface layer_surf_interface = {
	.set_size = layer_surface_set_size,
	.set_anchor = layer_surface_set_anchor,
	.set_exclusive_zone = layer_surface_set_exclusive_zone,
	.set_margin = layer_surface_set_margin,
	.set_keyboard_interactivity = layer_surface_set_keyboard_interactivity,
	.get_popup = layer_surface_get_popup,
	.ack_configure = layer_surface_ack_configure,
	.destroy = layer_surface_destroy,
	.set_layer = layer_surface_set_layer,
	.set_exclusive_edge = layer_surface_set_exclusive_edge,
};

static void layer_surface_destroy_handler(struct wl_resource *resource) {
	owl_layer_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) return;

	if (surface->mapped) {
		surface->mapped = false;
		owl_invoke_layer_surface_callback(surface->display, OWL_LAYER_SURFACE_EVENT_UNMAP, surface);
	}

	owl_invoke_layer_surface_callback(surface->display, OWL_LAYER_SURFACE_EVENT_DESTROY, surface);

	owl_display *display = surface->display;
	for (int i = 0; i < display->layer_surface_count; i++) {
		if (display->layer_surfaces[i] == surface) {
			for (int j = i; j < display->layer_surface_count - 1; j++) {
				display->layer_surfaces[j] = display->layer_surfaces[j + 1];
			}
			display->layer_surface_count--;
			break;
		}
	}

	free(surface->namespace);
	free(surface);
}

static void owl_layer_surface_send_configure_impl(owl_layer_surface *ls, uint32_t width, uint32_t height) {
	if (!ls || !ls->layer_surface_resource) return;

	owl_output *output = ls->output;
	if (!output && ls->display->output_count > 0) {
		output = ls->display->outputs[0];
	}
	if (!output) return;

	int out_w = output->width;
	int out_h = output->height;

	if (width == 0) {
		if ((ls->anchor & (OWL_ANCHOR_LEFT | OWL_ANCHOR_RIGHT)) == (OWL_ANCHOR_LEFT | OWL_ANCHOR_RIGHT)) {
			width = out_w - ls->margin_left - ls->margin_right;
		}
	}
	if (height == 0) {
		if ((ls->anchor & (OWL_ANCHOR_TOP | OWL_ANCHOR_BOTTOM)) == (OWL_ANCHOR_TOP | OWL_ANCHOR_BOTTOM)) {
			height = out_h - ls->margin_top - ls->margin_bottom;
		}
	}

	ls->configured_width = width;
	ls->configured_height = height;
	ls->pending_serial = layer_surface_configure_serial;

	zwlr_layer_surface_v1_send_configure(ls->layer_surface_resource, layer_surface_configure_serial++, width, height);
}

static void layer_shell_get_layer_surface(struct wl_client *client, struct wl_resource *resource,
                                          uint32_t id, struct wl_resource *surface_resource,
                                          struct wl_resource *output_resource, uint32_t layer,
                                          const char *namespace) {
	owl_display *display = wl_resource_get_user_data(resource);
	owl_surface *wl_surface = owl_surface_from_resource(surface_resource);

	if (!wl_surface) {
		wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_ROLE, "surface is null");
		return;
	}

	if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
		wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER, "invalid layer %d", layer);
		return;
	}

	owl_layer_surface *ls = calloc(1, sizeof(owl_layer_surface));
	if (!ls) {
		wl_resource_post_no_memory(resource);
		return;
	}

	ls->display = display;
	ls->surface = wl_surface;
	ls->layer = layer;
	ls->namespace = namespace ? strdup(namespace) : NULL;
	ls->anchor = 0;
	ls->exclusive_zone = 0;
	ls->keyboard_interactivity = OWL_KEYBOARD_INTERACTIVITY_NONE;
	ls->mapped = false;

	if (output_resource) {
		for (int i = 0; i < display->output_count; i++) {
			if (display->outputs[i]->wl_output_global) {
				ls->output = display->outputs[i];
				break;
			}
		}
	}

	uint32_t version = wl_resource_get_version(resource);
	ls->layer_surface_resource = wl_resource_create(client, &zwlr_layer_surface_v1_interface, version, id);
	if (!ls->layer_surface_resource) {
		free(ls->namespace);
		free(ls);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(ls->layer_surface_resource, &layer_surf_interface, ls, layer_surface_destroy_handler);

	if (display->layer_surface_count < OWL_MAX_LAYER_SURFACES) {
		display->layer_surfaces[display->layer_surface_count++] = ls;
	}

	owl_invoke_layer_surface_callback(display, OWL_LAYER_SURFACE_EVENT_CREATE, ls);
	ls->initial_configure_sent = false;
}

static void layer_shell_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_interface = {
	.get_layer_surface = layer_shell_get_layer_surface,
	.destroy = layer_shell_destroy,
};

static void layer_shell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	owl_display *display = data;
	uint32_t bound_version = version < 4 ? version : 4;

	struct wl_resource *resource = wl_resource_create(client, &zwlr_layer_shell_v1_interface, bound_version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &layer_shell_interface, display, NULL);
}

void owl_layer_shell_init(owl_display *display) {
	display->layer_shell_global = wl_global_create(display->wayland_display,
		&zwlr_layer_shell_v1_interface, 4, display, layer_shell_bind);

	if (!display->layer_shell_global) {
		fprintf(stderr, "owl: failed to create layer_shell global\n");
		return;
	}
	fprintf(stderr, "owl: layer-shell initialized\n");
}

void owl_layer_shell_cleanup(owl_display *display) {
	if (display->layer_shell_global) {
		wl_global_destroy(display->layer_shell_global);
		display->layer_shell_global = NULL;
	}
}

void owl_layer_surface_send_initial_configure(owl_layer_surface *ls) {
	if (!ls || ls->initial_configure_sent) return;
	ls->initial_configure_sent = true;
	owl_layer_surface_send_configure_impl(ls, ls->width, ls->height);
}

owl_layer_surface **owl_display_get_layer_surfaces(owl_display *display, int *count) {
	if (!display || !count) {
		if (count) *count = 0;
		return NULL;
	}
	*count = display->layer_surface_count;
	return display->layer_surfaces;
}

/* ==========================================================================
 * Output protocol
 * ========================================================================== */

static struct wl_global *xdg_output_manager_global = NULL;

static void wl_output_release(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static const struct wl_output_interface output_impl = {
	.release = wl_output_release,
};

static void wl_output_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	owl_output *output = data;
	uint32_t bound_version = version < 4 ? version : 4;

	struct wl_resource *resource = wl_resource_create(client, &wl_output_interface, bound_version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &output_impl, output, NULL);

	wl_output_send_geometry(resource, output->x, output->y, 0, 0,
		WL_OUTPUT_SUBPIXEL_UNKNOWN, "owl", output->name, WL_OUTPUT_TRANSFORM_NORMAL);

	wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
		output->width, output->height, ((drmModeModeInfo *)output->drm_mode)->vrefresh * 1000);

	if (bound_version >= 2) wl_output_send_scale(resource, 1);
	if (bound_version >= 4) {
		wl_output_send_name(resource, output->name);
		wl_output_send_description(resource, output->name);
	}
	if (bound_version >= 2) wl_output_send_done(resource);
}

static void xdg_output_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static const struct zxdg_output_v1_interface xdg_output_impl = {
	.destroy = xdg_output_destroy,
};

static void xdg_output_manager_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void xdg_output_manager_get_xdg_output(struct wl_client *client, struct wl_resource *resource,
                                               uint32_t id, struct wl_resource *output_resource) {
	owl_output *output = wl_resource_get_user_data(output_resource);

	uint32_t version = wl_resource_get_version(resource);
	struct wl_resource *xdg_output = wl_resource_create(client, &zxdg_output_v1_interface, version, id);
	if (!xdg_output) {
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(xdg_output, &xdg_output_impl, output, NULL);

	zxdg_output_v1_send_logical_position(xdg_output, output->x, output->y);
	zxdg_output_v1_send_logical_size(xdg_output, output->width, output->height);

	if (version >= 2) {
		zxdg_output_v1_send_name(xdg_output, output->name);
		zxdg_output_v1_send_description(xdg_output, output->name);
	}
	if (version < 3) {
		zxdg_output_v1_send_done(xdg_output);
	}
}

static const struct zxdg_output_manager_v1_interface xdg_output_manager_impl = {
	.destroy = xdg_output_manager_destroy,
	.get_xdg_output = xdg_output_manager_get_xdg_output,
};

static void xdg_output_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	owl_display *display = data;
	uint32_t bound_version = version < 3 ? version : 3;

	struct wl_resource *resource = wl_resource_create(client, &zxdg_output_manager_v1_interface, bound_version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &xdg_output_manager_impl, display, NULL);
}

static owl_output *create_output(owl_display *display, drmModeConnector *connector, drmModeCrtc *crtc) {
	owl_output *output = calloc(1, sizeof(owl_output));
	if (!output) return NULL;

	output->display = display;
	output->drm_connector_id = connector->connector_id;
	output->drm_crtc_id = crtc->crtc_id;
	memcpy(output->drm_mode, &connector->modes[0], sizeof(drmModeModeInfo));
	output->width = ((drmModeModeInfo *)output->drm_mode)->hdisplay;
	output->height = ((drmModeModeInfo *)output->drm_mode)->vdisplay;
	output->x = crtc->x;
	output->y = crtc->y;

	const char *connector_types[] = {
		"Unknown", "VGA", "DVII", "DVID", "DVIA", "Composite", "SVIDEO",
		"LVDS", "Component", "9PinDIN", "DisplayPort", "HDMIA", "HDMIB",
		"TV", "eDP", "VIRTUAL", "DSI", "DPI"
	};

	const char *type_name = "Unknown";
	if (connector->connector_type < sizeof(connector_types) / sizeof(connector_types[0])) {
		type_name = connector_types[connector->connector_type];
	}

	char name_buffer[64];
	snprintf(name_buffer, sizeof(name_buffer), "%s-%d", type_name, connector->connector_type_id);
	output->name = strdup(name_buffer);

	output->gbm_surface = gbm_surface_create(display->gbm_device, output->width, output->height,
		GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

	if (!output->gbm_surface) {
		free(output->name);
		free(output);
		return NULL;
	}

	output->egl_surface = eglCreateWindowSurface(display->egl_display, display->egl_config,
		(EGLNativeWindowType)output->gbm_surface, NULL);

	if (output->egl_surface == EGL_NO_SURFACE) {
		gbm_surface_destroy(output->gbm_surface);
		free(output->name);
		free(output);
		return NULL;
	}

	output->wl_output_global = wl_global_create(display->wayland_display,
		&wl_output_interface, 4, output, wl_output_bind);

	if (!output->wl_output_global) {
		eglDestroySurface(display->egl_display, output->egl_surface);
		gbm_surface_destroy(output->gbm_surface);
		free(output->name);
		free(output);
		return NULL;
	}

	fprintf(stderr, "owl: output %s: %dx%d\n", output->name, output->width, output->height);
	return output;
}

static void destroy_output(owl_output *output) {
	if (!output) return;

	if (output->wl_output_global) wl_global_destroy(output->wl_output_global);
	if (output->current_bo) gbm_surface_release_buffer(output->gbm_surface, output->current_bo);
	if (output->egl_surface) eglDestroySurface(output->display->egl_display, output->egl_surface);
	if (output->gbm_surface) gbm_surface_destroy(output->gbm_surface);

	free(output->name);
	free(output);
}

void owl_output_init(owl_display *display) {
	xdg_output_manager_global = wl_global_create(display->wayland_display,
		&zxdg_output_manager_v1_interface, 3, display, xdg_output_manager_bind);

	drmModeRes *resources = drmModeGetResources(display->drm_fd);
	if (!resources) {
		fprintf(stderr, "owl: failed to get DRM resources\n");
		return;
	}

	for (int ci = 0; ci < resources->count_connectors; ci++) {
		drmModeConnector *connector = drmModeGetConnector(display->drm_fd, resources->connectors[ci]);
		if (!connector) continue;

		if (connector->connection != DRM_MODE_CONNECTED || connector->count_modes == 0) {
			drmModeFreeConnector(connector);
			continue;
		}

		drmModeEncoder *encoder = NULL;
		if (connector->encoder_id) {
			encoder = drmModeGetEncoder(display->drm_fd, connector->encoder_id);
		}
		if (!encoder) {
			for (int ei = 0; ei < connector->count_encoders; ei++) {
				encoder = drmModeGetEncoder(display->drm_fd, connector->encoders[ei]);
				if (encoder) break;
			}
		}
		if (!encoder) {
			drmModeFreeConnector(connector);
			continue;
		}

		drmModeCrtc *crtc = NULL;
		if (encoder->crtc_id) {
			crtc = drmModeGetCrtc(display->drm_fd, encoder->crtc_id);
		}
		if (!crtc) {
			for (int ci2 = 0; ci2 < resources->count_crtcs; ci2++) {
				if (encoder->possible_crtcs & (1 << ci2)) {
					crtc = drmModeGetCrtc(display->drm_fd, resources->crtcs[ci2]);
					if (crtc) break;
				}
			}
		}
		if (!crtc) {
			drmModeFreeEncoder(encoder);
			drmModeFreeConnector(connector);
			continue;
		}

		if (!crtc->mode_valid) {
			drmModeSetCrtc(display->drm_fd, crtc->crtc_id, -1, 0, 0,
			               &connector->connector_id, 1, &connector->modes[0]);
			drmModeFreeCrtc(crtc);
			crtc = drmModeGetCrtc(display->drm_fd, encoder->crtc_id ? encoder->crtc_id : resources->crtcs[0]);
		}

		if (display->output_count < OWL_MAX_OUTPUTS) {
			owl_output *output = create_output(display, connector, crtc);
			if (output) {
				display->outputs[display->output_count++] = output;
				owl_invoke_output_callback(display, OWL_OUTPUT_EVENT_CONNECT, output);
			}
		}

		drmModeFreeCrtc(crtc);
		drmModeFreeEncoder(encoder);
		drmModeFreeConnector(connector);
	}

	drmModeFreeResources(resources);
}

void owl_output_cleanup(owl_display *display) {
	for (int i = 0; i < display->output_count; i++) {
		owl_invoke_output_callback(display, OWL_OUTPUT_EVENT_DISCONNECT, display->outputs[i]);
		destroy_output(display->outputs[i]);
		display->outputs[i] = NULL;
	}
	display->output_count = 0;

	if (xdg_output_manager_global) {
		wl_global_destroy(xdg_output_manager_global);
		xdg_output_manager_global = NULL;
	}
}

owl_output **owl_display_get_outputs(owl_display *display, int *count) {
	if (!display || !count) {
		if (count) *count = 0;
		return NULL;
	}
	*count = display->output_count;
	return display->outputs;
}

/* ==========================================================================
 * Workspace protocol
 * ========================================================================== */

typedef struct {
	struct wl_resource *resource;
	owl_workspace *workspace;
	struct wl_list link;
} workspace_resource;

static void workspace_handle_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void workspace_handle_activate(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	workspace_resource *ws_res = wl_resource_get_user_data(resource);
	if (ws_res && ws_res->workspace && ws_res->workspace->display) {
		owl_invoke_workspace_callback(ws_res->workspace->display, OWL_WORKSPACE_EVENT_ACTIVATE, ws_res->workspace);
	}
}

static void workspace_handle_deactivate(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	workspace_resource *ws_res = wl_resource_get_user_data(resource);
	if (ws_res && ws_res->workspace && ws_res->workspace->display) {
		owl_invoke_workspace_callback(ws_res->workspace->display, OWL_WORKSPACE_EVENT_DEACTIVATE, ws_res->workspace);
	}
}

static void workspace_handle_assign(struct wl_client *client, struct wl_resource *resource,
                                     struct wl_resource *workspace_group) {
	(void)client; (void)resource; (void)workspace_group;
}

static void workspace_handle_remove(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	workspace_resource *ws_res = wl_resource_get_user_data(resource);
	if (ws_res && ws_res->workspace && ws_res->workspace->display) {
		owl_invoke_workspace_callback(ws_res->workspace->display, OWL_WORKSPACE_EVENT_REMOVE, ws_res->workspace);
	}
}

static const struct ext_workspace_handle_v1_interface workspace_impl = {
	.destroy = workspace_handle_destroy,
	.activate = workspace_handle_activate,
	.deactivate = workspace_handle_deactivate,
	.assign = workspace_handle_assign,
	.remove = workspace_handle_remove,
};

static void workspace_resource_destroy(struct wl_resource *resource) {
	workspace_resource *ws_resource = wl_resource_get_user_data(resource);
	if (ws_resource) {
		wl_list_remove(&ws_resource->link);
		free(ws_resource);
	}
}

static void workspace_group_handle_create_workspace(struct wl_client *client,
                                                     struct wl_resource *resource, const char *name) {
	(void)client; (void)resource; (void)name;
}

static void workspace_group_handle_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static const struct ext_workspace_group_handle_v1_interface workspace_group_impl = {
	.create_workspace = workspace_group_handle_create_workspace,
	.destroy = workspace_group_handle_destroy,
};

static void workspace_group_resource_destroy(struct wl_resource *resource) {
	workspace_resource *ws_resource = wl_resource_get_user_data(resource);
	if (ws_resource) {
		wl_list_remove(&ws_resource->link);
		free(ws_resource);
	}
}

static void manager_handle_commit(struct wl_client *client, struct wl_resource *resource) {
	(void)client; (void)resource;
}

static void manager_handle_stop(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	ext_workspace_manager_v1_send_finished(resource);
}

static const struct ext_workspace_manager_v1_interface workspace_manager_impl = {
	.commit = manager_handle_commit,
	.stop = manager_handle_stop,
};

static void manager_resource_destroy(struct wl_resource *resource) {
	workspace_resource *ws_resource = wl_resource_get_user_data(resource);
	if (ws_resource) {
		wl_list_remove(&ws_resource->link);
		free(ws_resource);
	}
}

static void send_workspace_to_client(owl_display *display, owl_workspace *workspace,
                                      struct wl_resource *manager_resource,
                                      struct wl_resource *group_resource) {
	struct wl_client *client = wl_resource_get_client(manager_resource);
	uint32_t version = wl_resource_get_version(manager_resource);

	struct wl_resource *ws_resource = wl_resource_create(client, &ext_workspace_handle_v1_interface, version, 0);
	if (!ws_resource) return;

	workspace_resource *ws_res = calloc(1, sizeof(workspace_resource));
	if (!ws_res) {
		wl_resource_destroy(ws_resource);
		return;
	}
	ws_res->resource = ws_resource;
	ws_res->workspace = workspace;
	wl_list_insert(&workspace->resources, &ws_res->link);

	wl_resource_set_implementation(ws_resource, &workspace_impl, ws_res, workspace_resource_destroy);

	ext_workspace_manager_v1_send_workspace(manager_resource, ws_resource);

	if (workspace->id) {
		ext_workspace_handle_v1_send_id(ws_resource, workspace->id);
	}
	ext_workspace_handle_v1_send_name(ws_resource, workspace->name);

	struct wl_array coords;
	wl_array_init(&coords);
	int32_t *coord = wl_array_add(&coords, sizeof(int32_t));
	if (coord) {
		*coord = workspace->coordinate;
		ext_workspace_handle_v1_send_coordinates(ws_resource, &coords);
	}
	wl_array_release(&coords);

	ext_workspace_handle_v1_send_state(ws_resource, workspace->state);

	uint32_t capabilities = EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE |
	                        EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_DEACTIVATE;
	ext_workspace_handle_v1_send_capabilities(ws_resource, capabilities);

	ext_workspace_group_handle_v1_send_workspace_enter(group_resource, ws_resource);
}

static void workspace_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	owl_display *display = data;
	uint32_t bound_version = version < 1 ? version : 1;

	struct wl_resource *resource = wl_resource_create(client, &ext_workspace_manager_v1_interface, bound_version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	workspace_resource *ws_resource = calloc(1, sizeof(workspace_resource));
	if (!ws_resource) {
		wl_resource_destroy(resource);
		wl_client_post_no_memory(client);
		return;
	}
	ws_resource->resource = resource;
	ws_resource->workspace = NULL;
	wl_list_insert(&display->workspace_manager_resources, &ws_resource->link);

	wl_resource_set_implementation(resource, &workspace_manager_impl, ws_resource, manager_resource_destroy);

	struct wl_resource *group_resource = wl_resource_create(client,
		&ext_workspace_group_handle_v1_interface, bound_version, 0);
	if (!group_resource) return;

	workspace_resource *group_res = calloc(1, sizeof(workspace_resource));
	if (!group_res) {
		wl_resource_destroy(group_resource);
		return;
	}
	group_res->resource = group_resource;
	group_res->workspace = NULL;
	wl_list_insert(&display->workspace_group_resources, &group_res->link);

	wl_resource_set_implementation(group_resource, &workspace_group_impl, group_res, workspace_group_resource_destroy);

	ext_workspace_manager_v1_send_workspace_group(resource, group_resource);

	uint32_t group_capabilities = 0;
	ext_workspace_group_handle_v1_send_capabilities(group_resource, group_capabilities);

	owl_workspace *workspace;
	wl_list_for_each(workspace, &display->workspaces, link) {
		send_workspace_to_client(display, workspace, resource, group_resource);
	}

	ext_workspace_manager_v1_send_done(resource);
}

void owl_workspace_init(owl_display *display) {
	wl_list_init(&display->workspaces);
	wl_list_init(&display->workspace_manager_resources);
	wl_list_init(&display->workspace_group_resources);
	display->workspace_count = 0;

	for (int i = 0; i < 3; i++) {
		display->workspace_callback_count[i] = 0;
	}

	display->workspace_manager_global = wl_global_create(display->wayland_display,
		&ext_workspace_manager_v1_interface, 1, display, workspace_manager_bind);

	if (!display->workspace_manager_global) {
		fprintf(stderr, "owl: failed to create ext_workspace_manager_v1 global\n");
	}
}

void owl_workspace_cleanup(owl_display *display) {
	owl_workspace *workspace, *tmp;
	wl_list_for_each_safe(workspace, tmp, &display->workspaces, link) {
		wl_list_remove(&workspace->link);
		free(workspace->name);
		free(workspace->id);
		free(workspace);
	}

	if (display->workspace_manager_global) {
		wl_global_destroy(display->workspace_manager_global);
		display->workspace_manager_global = NULL;
	}
}

owl_workspace *owl_workspace_create(owl_display *display, const char *name) {
	owl_workspace *workspace = calloc(1, sizeof(owl_workspace));
	if (!workspace) return NULL;

	workspace->display = display;
	workspace->name = strdup(name);
	workspace->id = strdup(name);
	workspace->state = 0;
	workspace->coordinate = display->workspace_count;
	wl_list_init(&workspace->resources);
	wl_list_insert(&display->workspaces, &workspace->link);
	display->workspace_count++;

	workspace_resource *mgr_res;
	wl_list_for_each(mgr_res, &display->workspace_manager_resources, link) {
		workspace_resource *grp_res;
		wl_list_for_each(grp_res, &display->workspace_group_resources, link) {
			if (wl_resource_get_client(mgr_res->resource) == wl_resource_get_client(grp_res->resource)) {
				send_workspace_to_client(display, workspace, mgr_res->resource, grp_res->resource);
				ext_workspace_manager_v1_send_done(mgr_res->resource);
				break;
			}
		}
	}

	return workspace;
}

void owl_workspace_destroy(owl_workspace *workspace) {
	if (!workspace) return;

	workspace_resource *ws_res, *tmp;
	wl_list_for_each_safe(ws_res, tmp, &workspace->resources, link) {
		ext_workspace_handle_v1_send_removed(ws_res->resource);
	}

	workspace_resource *mgr_res;
	wl_list_for_each(mgr_res, &workspace->display->workspace_manager_resources, link) {
		ext_workspace_manager_v1_send_done(mgr_res->resource);
	}

	wl_list_for_each_safe(ws_res, tmp, &workspace->resources, link) {
		wl_list_remove(&ws_res->link);
		wl_resource_set_user_data(ws_res->resource, NULL);
		free(ws_res);
	}

	wl_list_remove(&workspace->link);
	workspace->display->workspace_count--;
	free(workspace->name);
	free(workspace->id);
	free(workspace);
}

void owl_workspace_set_state(owl_workspace *workspace, uint32_t state) {
	if (!workspace || workspace->state == state) return;

	workspace->state = state;

	workspace_resource *ws_res;
	wl_list_for_each(ws_res, &workspace->resources, link) {
		ext_workspace_handle_v1_send_state(ws_res->resource, state);
	}
}

void owl_workspace_set_coordinates(owl_workspace *workspace, int32_t x) {
	if (!workspace) return;

	workspace->coordinate = x;

	workspace_resource *ws_res;
	wl_list_for_each(ws_res, &workspace->resources, link) {
		struct wl_array coords;
		wl_array_init(&coords);
		int32_t *coord = wl_array_add(&coords, sizeof(int32_t));
		if (coord) {
			*coord = x;
			ext_workspace_handle_v1_send_coordinates(ws_res->resource, &coords);
		}
		wl_array_release(&coords);
	}
}

void owl_workspace_commit(owl_display *display) {
	if (!display) return;

	workspace_resource *mgr_res;
	wl_list_for_each(mgr_res, &display->workspace_manager_resources, link) {
		ext_workspace_manager_v1_send_done(mgr_res->resource);
	}
}

/* ==========================================================================
 * Decoration protocol
 * ========================================================================== */

static struct wl_global *decoration_global = NULL;

static void decoration_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void decoration_set_mode(struct wl_client *client, struct wl_resource *resource, uint32_t mode) {
	(void)client; (void)mode;
	zxdg_toplevel_decoration_v1_send_configure(resource, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void decoration_unset_mode(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	zxdg_toplevel_decoration_v1_send_configure(resource, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_toplevel_decoration_v1_interface decoration_impl = {
	.destroy = decoration_destroy,
	.set_mode = decoration_set_mode,
	.unset_mode = decoration_unset_mode,
};

static void deco_manager_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client; wl_resource_destroy(resource);
}

static void deco_manager_get_toplevel_decoration(struct wl_client *client, struct wl_resource *resource,
                                                  uint32_t id, struct wl_resource *toplevel) {
	(void)toplevel;

	struct wl_resource *deco = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface, 1, id);
	if (!deco) {
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(deco, &decoration_impl, NULL, NULL);
	zxdg_toplevel_decoration_v1_send_configure(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_decoration_manager_v1_interface deco_manager_impl = {
	.destroy = deco_manager_destroy,
	.get_toplevel_decoration = deco_manager_get_toplevel_decoration,
};

static void deco_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	(void)data; (void)version;

	struct wl_resource *resource = wl_resource_create(client, &zxdg_decoration_manager_v1_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &deco_manager_impl, NULL, NULL);
}

void owl_decoration_init(owl_display *display) {
	decoration_global = wl_global_create(display->wayland_display,
		&zxdg_decoration_manager_v1_interface, 1, display, deco_manager_bind);

	if (!decoration_global) {
		fprintf(stderr, "owl: failed to create xdg-decoration global\n");
		return;
	}
	fprintf(stderr, "owl: xdg-decoration initialized\n");
}

void owl_decoration_cleanup(owl_display *display) {
	(void)display;
	if (decoration_global) {
		wl_global_destroy(decoration_global);
		decoration_global = NULL;
	}
}

/* ==========================================================================
 * Display lifecycle
 * ========================================================================== */

static void client_destroyed(struct wl_listener *listener, void *data) {
	(void)data;
	wl_list_remove(&listener->link);
	free(listener);
}

static void client_created(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wl_client *client = data;

	struct wl_listener *destroy_listener = malloc(sizeof(struct wl_listener));
	if (destroy_listener) {
		destroy_listener->notify = client_destroyed;
		wl_client_add_destroy_listener(client, destroy_listener);
	}
}

static struct wl_listener client_created_listener = {
	.notify = client_created,
};

static int open_drm_device(void) {
	const char *drm_paths[] = { "/dev/dri/card0", "/dev/dri/card1", NULL };

	for (int i = 0; drm_paths[i] != NULL; i++) {
		int fd = open(drm_paths[i], O_RDWR | O_CLOEXEC);
		if (fd < 0) continue;

		if (drmSetMaster(fd) < 0) {
			close(fd);
			continue;
		}

		uint64_t has_dumb = 0;
		if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 || !has_dumb) {
			drmDropMaster(fd);
			close(fd);
			continue;
		}

		return fd;
	}

	return -1;
}

static bool init_gbm(owl_display *display) {
	display->gbm_device = gbm_create_device(display->drm_fd);
	if (!display->gbm_device) {
		fprintf(stderr, "owl: failed to create GBM device\n");
		return false;
	}
	return true;
}

static PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display_ext = NULL;

static bool init_egl(owl_display *display) {
	get_platform_display_ext = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
		eglGetProcAddress("eglGetPlatformDisplayEXT");

	if (get_platform_display_ext) {
		display->egl_display = get_platform_display_ext(EGL_PLATFORM_GBM_KHR, display->gbm_device, NULL);
	} else {
		display->egl_display = eglGetDisplay((EGLNativeDisplayType)display->gbm_device);
	}

	if (display->egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "owl: failed to get EGL display\n");
		return false;
	}

	EGLint major, minor;
	if (!eglInitialize(display->egl_display, &major, &minor)) {
		fprintf(stderr, "owl: failed to initialize EGL\n");
		return false;
	}

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		fprintf(stderr, "owl: failed to bind OpenGL ES API\n");
		return false;
	}

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLint num_configs;
	if (!eglChooseConfig(display->egl_display, config_attribs, &display->egl_config, 1, &num_configs) || num_configs < 1) {
		fprintf(stderr, "owl: failed to choose EGL config\n");
		return false;
	}

	EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	display->egl_context = eglCreateContext(display->egl_display, display->egl_config, EGL_NO_CONTEXT, context_attribs);

	if (display->egl_context == EGL_NO_CONTEXT) {
		fprintf(stderr, "owl: failed to create EGL context\n");
		return false;
	}

	return true;
}

static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
                              unsigned int tv_usec, void *user_data) {
	(void)fd; (void)sequence; (void)tv_sec; (void)tv_usec;
	owl_output *output = user_data;

	if (output) {
		output->page_flip_pending = false;
		if (output->current_bo) {
			gbm_surface_release_buffer(output->gbm_surface, output->current_bo);
		}
		output->current_bo = output->next_bo;
		output->next_bo = NULL;

		if (output->display) {
			owl_render_frame(output->display, output);
		}
	}
}

static int handle_drm_event(int fd, uint32_t mask, void *data) {
	(void)mask; (void)data;
	drmEventContext event_context = {
		.version = 2,
		.page_flip_handler = page_flip_handler,
	};
	drmHandleEvent(fd, &event_context);
	return 0;
}

owl_display *owl_display_create(void) {
	owl_display *display = calloc(1, sizeof(owl_display));
	if (!display) return NULL;

	wl_list_init(&display->windows);

	display->wayland_display = wl_display_create();
	if (!display->wayland_display) {
		free(display);
		return NULL;
	}

	display->socket_name = wl_display_add_socket_auto(display->wayland_display);
	if (!display->socket_name) {
		fprintf(stderr, "owl: failed to add wayland socket\n");
		wl_display_destroy(display->wayland_display);
		free(display);
		return NULL;
	}

	fprintf(stderr, "owl: listening on %s\n", display->socket_name);

	display->event_loop = wl_display_get_event_loop(display->wayland_display);

	display->drm_fd = open_drm_device();
	if (display->drm_fd < 0) {
		fprintf(stderr, "owl: failed to open DRM device\n");
		wl_display_destroy(display->wayland_display);
		free(display);
		return NULL;
	}

	if (!init_gbm(display)) {
		close(display->drm_fd);
		wl_display_destroy(display->wayland_display);
		free(display);
		return NULL;
	}

	if (!init_egl(display)) {
		gbm_device_destroy(display->gbm_device);
		close(display->drm_fd);
		wl_display_destroy(display->wayland_display);
		free(display);
		return NULL;
	}

	display->drm_event_source = wl_event_loop_add_fd(display->event_loop, display->drm_fd,
		WL_EVENT_READABLE, handle_drm_event, display);

	owl_output_init(display);
	owl_input_init(display);
	owl_seat_init(display);
	owl_surface_init(display);
	owl_xdg_shell_init(display);
	owl_decoration_init(display);
	owl_layer_shell_init(display);
	owl_workspace_init(display);
	owl_render_init(display);

	wl_display_add_client_created_listener(display->wayland_display, &client_created_listener);

	display->running = false;

	return display;
}

void owl_display_destroy(owl_display *display) {
	if (!display) return;

	owl_render_cleanup(display);
	owl_workspace_cleanup(display);
	owl_layer_shell_cleanup(display);
	owl_decoration_cleanup(display);
	owl_xdg_shell_cleanup(display);
	owl_surface_cleanup(display);
	owl_seat_cleanup(display);
	owl_input_cleanup(display);
	owl_output_cleanup(display);

	if (display->drm_event_source) wl_event_source_remove(display->drm_event_source);
	if (display->egl_context) eglDestroyContext(display->egl_display, display->egl_context);
	if (display->egl_display) eglTerminate(display->egl_display);
	if (display->gbm_device) gbm_device_destroy(display->gbm_device);
	if (display->drm_fd >= 0) {
		drmDropMaster(display->drm_fd);
		close(display->drm_fd);
	}
	if (display->wayland_display) wl_display_destroy(display->wayland_display);

	free(display);
}

void owl_display_run(owl_display *display) {
	if (!display) return;

	display->running = true;

	for (int i = 0; i < display->output_count; i++) {
		owl_render_frame(display, display->outputs[i]);
	}

	while (display->running) {
		wl_display_flush_clients(display->wayland_display);
		wl_event_loop_dispatch(display->event_loop, -1);
	}
}

void owl_display_terminate(owl_display *display) {
	if (!display) return;
	display->running = false;
}

const char *owl_display_get_socket(owl_display *display) {
	return display ? display->socket_name : NULL;
}

void owl_display_get_pointer(owl_display *display, int *x, int *y) {
	if (x) *x = display ? (int)display->pointer_x : 0;
	if (y) *y = display ? (int)display->pointer_y : 0;
}
