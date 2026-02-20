#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <wayland-server-core.h>
#include "xdg-shell-protocol.h"
#include "xdg-shell-protocol.c"

static FILE* owl_log = NULL;
static void owl_debug(const char* fmt, ...) {
    if (!owl_log) owl_log = fopen("/tmp/owl_debug.log", "w");
    if (owl_log) {
        va_list args;
        va_start(args, fmt);
        vfprintf(owl_log, fmt, args);
        va_end(args);
        fflush(owl_log);
    }
}

static void xdg_toplevel_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void xdg_toplevel_set_parent(struct wl_client* client, struct wl_resource* resource,
                                    struct wl_resource* parent) {
    (void)client;
    (void)resource;
    (void)parent;
}

static void xdg_toplevel_set_title(struct wl_client* client, struct wl_resource* resource,
                                   const char* title) {
    (void)client;
    Owl_Window* window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }

    free(window->title);
    window->title = title ? strdup(title) : NULL;

    owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_TITLE_CHANGE, window);
}

static void xdg_toplevel_set_app_id(struct wl_client* client, struct wl_resource* resource,
                                    const char* app_id) {
    (void)client;
    Owl_Window* window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }

    free(window->app_id);
    window->app_id = app_id ? strdup(app_id) : NULL;
}

static void xdg_toplevel_show_window_menu(struct wl_client* client, struct wl_resource* resource,
                                          struct wl_resource* seat, uint32_t serial,
                                          int32_t x, int32_t y) {
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
    (void)x;
    (void)y;
}

static void xdg_toplevel_move(struct wl_client* client, struct wl_resource* resource,
                              struct wl_resource* seat, uint32_t serial) {
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
}

static void xdg_toplevel_resize(struct wl_client* client, struct wl_resource* resource,
                                struct wl_resource* seat, uint32_t serial, uint32_t edges) {
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
    (void)edges;
}

static void xdg_toplevel_set_max_size(struct wl_client* client, struct wl_resource* resource,
                                      int32_t width, int32_t height) {
    (void)client;
    (void)resource;
    (void)width;
    (void)height;
}

static void xdg_toplevel_set_min_size(struct wl_client* client, struct wl_resource* resource,
                                      int32_t width, int32_t height) {
    (void)client;
    (void)resource;
    (void)width;
    (void)height;
}

static void xdg_toplevel_set_maximized(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    (void)resource;
}

static void xdg_toplevel_unset_maximized(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    (void)resource;
}

static void xdg_toplevel_set_fullscreen(struct wl_client* client, struct wl_resource* resource,
                                        struct wl_resource* output) {
    (void)client;
    (void)output;

    Owl_Window* window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }

    window->fullscreen = true;
    owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_FULLSCREEN, window);
}

static void xdg_toplevel_unset_fullscreen(struct wl_client* client, struct wl_resource* resource) {
    (void)client;

    Owl_Window* window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }

    window->fullscreen = false;
    owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_FULLSCREEN, window);
}

static void xdg_toplevel_set_minimized(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    (void)resource;
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

static void xdg_toplevel_destroy_handler(struct wl_resource* resource) {
    Owl_Window* window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }

    window->xdg_toplevel_resource = NULL;
}

static void send_toplevel_configure(Owl_Window* window) {
    struct wl_array states;
    wl_array_init(&states);

    if (window->focused) {
        uint32_t* state = wl_array_add(&states, sizeof(uint32_t));
        *state = XDG_TOPLEVEL_STATE_ACTIVATED;
    }

    if (window->fullscreen) {
        uint32_t* state = wl_array_add(&states, sizeof(uint32_t));
        *state = XDG_TOPLEVEL_STATE_FULLSCREEN;
    }

    xdg_toplevel_send_configure(window->xdg_toplevel_resource,
                                window->width, window->height, &states);

    wl_array_release(&states);
}

static void xdg_surface_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void xdg_surface_get_toplevel(struct wl_client* client, struct wl_resource* resource,
                                     uint32_t id) {
    owl_debug("xdg_surface_get_toplevel called\n");
    Owl_Window* window = wl_resource_get_user_data(resource);
    if (!window) {
        owl_debug("  window is NULL!\n");
        return;
    }

    uint32_t version = wl_resource_get_version(resource);
    owl_debug("  creating toplevel resource version %d\n", version);
    window->xdg_toplevel_resource = wl_resource_create(client, &xdg_toplevel_interface, version, id);
    if (!window->xdg_toplevel_resource) {
        owl_debug("  failed to create toplevel resource\n");
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(window->xdg_toplevel_resource, &toplevel_interface,
                                   window, xdg_toplevel_destroy_handler);

    owl_debug("  sending initial configure\n");
    send_toplevel_configure(window);
    static uint32_t configure_serial = 1;
    window->pending_serial = configure_serial;
    window->pending_configure = true;
    xdg_surface_send_configure(window->xdg_surface_resource, configure_serial++);
    owl_debug("  configure sent\n");

    fprintf(stderr, "owl: xdg_toplevel created\n");
}

static void xdg_popup_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void xdg_popup_grab(struct wl_client* client, struct wl_resource* resource,
                           struct wl_resource* seat, uint32_t serial) {
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
}

static void xdg_popup_reposition(struct wl_client* client, struct wl_resource* resource,
                                 struct wl_resource* positioner, uint32_t token) {
    (void)client;
    (void)resource;
    (void)positioner;
    (void)token;
}

static const struct xdg_popup_interface popup_interface = {
    .destroy = xdg_popup_destroy,
    .grab = xdg_popup_grab,
    .reposition = xdg_popup_reposition,
};

static void xdg_surface_get_popup(struct wl_client* client, struct wl_resource* resource,
                                  uint32_t id, struct wl_resource* parent,
                                  struct wl_resource* positioner) {
    (void)parent;
    (void)positioner;

    uint32_t version = wl_resource_get_version(resource);
    struct wl_resource* popup = wl_resource_create(client, &xdg_popup_interface, version, id);
    if (!popup) {
        wl_resource_post_no_memory(resource);
        return;
    }
    wl_resource_set_implementation(popup, &popup_interface, NULL, NULL);

    xdg_popup_send_configure(popup, 0, 0, 100, 100);
    xdg_surface_send_configure(resource, 1);
}

static void xdg_surface_set_window_geometry(struct wl_client* client, struct wl_resource* resource,
                                            int32_t x, int32_t y, int32_t width, int32_t height) {
    (void)client;
    (void)x;
    (void)y;

    Owl_Window* window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }

    if (window->width != width || window->height != height) {
        window->width = width;
        window->height = height;
    }
}

static void xdg_surface_ack_configure(struct wl_client* client, struct wl_resource* resource,
                                      uint32_t serial) {
    (void)client;

    Owl_Window* window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }

    if (window->pending_serial == serial) {
        window->pending_configure = false;
    }
}

static const struct xdg_surface_interface surface_interface = {
    .destroy = xdg_surface_destroy,
    .get_toplevel = xdg_surface_get_toplevel,
    .get_popup = xdg_surface_get_popup,
    .set_window_geometry = xdg_surface_set_window_geometry,
    .ack_configure = xdg_surface_ack_configure,
};

static void xdg_surface_destroy_handler(struct wl_resource* resource) {
    Owl_Window* window = wl_resource_get_user_data(resource);
    if (!window) {
        return;
    }

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

static void xdg_positioner_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void xdg_positioner_set_size(struct wl_client* client, struct wl_resource* resource,
                                    int32_t width, int32_t height) {
    (void)client;
    (void)resource;
    (void)width;
    (void)height;
}

static void xdg_positioner_set_anchor_rect(struct wl_client* client, struct wl_resource* resource,
                                           int32_t x, int32_t y, int32_t width, int32_t height) {
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void xdg_positioner_set_anchor(struct wl_client* client, struct wl_resource* resource,
                                      uint32_t anchor) {
    (void)client;
    (void)resource;
    (void)anchor;
}

static void xdg_positioner_set_gravity(struct wl_client* client, struct wl_resource* resource,
                                       uint32_t gravity) {
    (void)client;
    (void)resource;
    (void)gravity;
}

static void xdg_positioner_set_constraint_adjustment(struct wl_client* client,
                                                     struct wl_resource* resource,
                                                     uint32_t constraint_adjustment) {
    (void)client;
    (void)resource;
    (void)constraint_adjustment;
}

static void xdg_positioner_set_offset(struct wl_client* client, struct wl_resource* resource,
                                      int32_t x, int32_t y) {
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
}

static void xdg_positioner_set_reactive(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    (void)resource;
}

static void xdg_positioner_set_parent_size(struct wl_client* client, struct wl_resource* resource,
                                           int32_t parent_width, int32_t parent_height) {
    (void)client;
    (void)resource;
    (void)parent_width;
    (void)parent_height;
}

static void xdg_positioner_set_parent_configure(struct wl_client* client,
                                                struct wl_resource* resource,
                                                uint32_t serial) {
    (void)client;
    (void)resource;
    (void)serial;
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

static void xdg_wm_base_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void xdg_wm_base_create_positioner(struct wl_client* client, struct wl_resource* resource,
                                          uint32_t id) {
    uint32_t version = wl_resource_get_version(resource);
    struct wl_resource* positioner = wl_resource_create(client, &xdg_positioner_interface, version, id);
    if (!positioner) {
        wl_resource_post_no_memory(resource);
        return;
    }
    wl_resource_set_implementation(positioner, &positioner_interface, NULL, NULL);
}

static void xdg_wm_base_get_xdg_surface(struct wl_client* client, struct wl_resource* resource,
                                        uint32_t id, struct wl_resource* surface_resource) {
    owl_debug("xdg_wm_base_get_xdg_surface called\n");
    Owl_Display* display = wl_resource_get_user_data(resource);
    Owl_Surface* surface = owl_surface_from_resource(surface_resource);

    if (!surface) {
        owl_debug("  surface is NULL!\n");
        wl_resource_post_error(resource, XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE,
                               "surface is null");
        return;
    }

    Owl_Window* window = calloc(1, sizeof(Owl_Window));
    if (!window) {
        owl_debug("  failed to alloc window\n");
        wl_resource_post_no_memory(resource);
        return;
    }

    window->display = display;
    window->surface = surface;
    window->width = 0;
    window->height = 0;

    uint32_t version = wl_resource_get_version(resource);
    owl_debug("  creating xdg_surface resource version %d\n", version);
    window->xdg_surface_resource = wl_resource_create(client, &xdg_surface_interface, version, id);
    if (!window->xdg_surface_resource) {
        owl_debug("  failed to create xdg_surface resource\n");
        free(window);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(window->xdg_surface_resource, &surface_interface,
                                   window, xdg_surface_destroy_handler);

    wl_list_insert(&display->windows, &window->link);
    display->window_count++;

    owl_debug("  xdg_surface created successfully\n");
    fprintf(stderr, "owl: xdg_surface created (total windows: %d)\n", display->window_count);
}

static void xdg_wm_base_pong(struct wl_client* client, struct wl_resource* resource,
                             uint32_t serial) {
    (void)client;
    (void)resource;
    (void)serial;
}

static const struct xdg_wm_base_interface wm_base_interface = {
    .destroy = xdg_wm_base_destroy,
    .create_positioner = xdg_wm_base_create_positioner,
    .get_xdg_surface = xdg_wm_base_get_xdg_surface,
    .pong = xdg_wm_base_pong,
};

static void wm_base_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    Owl_Display* display = data;

    uint32_t bound_version = version < 3 ? version : 3;
    struct wl_resource* resource = wl_resource_create(client, &xdg_wm_base_interface, bound_version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &wm_base_interface, display, NULL);
}

static struct wl_global* xdg_wm_base_global = NULL;

void owl_xdg_shell_init(Owl_Display* display) {
    xdg_wm_base_global = wl_global_create(display->wayland_display,
        &xdg_wm_base_interface, 3, display, wm_base_bind);

    if (!xdg_wm_base_global) {
        fprintf(stderr, "owl: failed to create xdg_wm_base global\n");
        return;
    }

    fprintf(stderr, "owl: xdg-shell initialized\n");
}

void owl_xdg_shell_cleanup(Owl_Display* display) {
    (void)display;

    if (xdg_wm_base_global) {
        wl_global_destroy(xdg_wm_base_global);
        xdg_wm_base_global = NULL;
    }
}

void owl_xdg_toplevel_send_configure(Owl_Window* window, int width, int height) {
    if (!window || !window->xdg_toplevel_resource || !window->xdg_surface_resource) {
        return;
    }

    window->width = width;
    window->height = height;

    send_toplevel_configure(window);

    static uint32_t serial = 1;
    window->pending_serial = serial;
    window->pending_configure = true;

    xdg_surface_send_configure(window->xdg_surface_resource, serial++);
}

void owl_xdg_toplevel_send_close(Owl_Window* window) {
    if (!window || !window->xdg_toplevel_resource) {
        return;
    }

    xdg_toplevel_send_close(window->xdg_toplevel_resource);
}

void owl_window_map(Owl_Window* window) {
    if (!window || window->mapped) {
        return;
    }

    window->mapped = true;
    owl_invoke_window_callback(window->display, OWL_WINDOW_EVENT_CREATE, window);

    for (int index = 0; index < window->display->output_count; index++) {
        owl_render_frame(window->display, window->display->outputs[index]);
    }
}
