#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.c"

static uint32_t layer_surface_configure_serial = 1;

static void layer_surface_set_size(struct wl_client* client, struct wl_resource* resource,
                                   uint32_t width, uint32_t height) {
    (void)client;
    Owl_Layer_Surface* surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->width = width;
    surface->height = height;
}

static void layer_surface_set_anchor(struct wl_client* client, struct wl_resource* resource,
                                     uint32_t anchor) {
    (void)client;
    Owl_Layer_Surface* surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->anchor = anchor;
}

static void layer_surface_set_exclusive_zone(struct wl_client* client, struct wl_resource* resource,
                                             int32_t zone) {
    (void)client;
    Owl_Layer_Surface* surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->exclusive_zone = zone;
}

static void layer_surface_set_margin(struct wl_client* client, struct wl_resource* resource,
                                     int32_t top, int32_t right, int32_t bottom, int32_t left) {
    (void)client;
    Owl_Layer_Surface* surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->margin_top = top;
    surface->margin_right = right;
    surface->margin_bottom = bottom;
    surface->margin_left = left;
}

static void layer_surface_set_keyboard_interactivity(struct wl_client* client,
                                                     struct wl_resource* resource,
                                                     uint32_t keyboard_interactivity) {
    (void)client;
    Owl_Layer_Surface* surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->keyboard_interactivity = keyboard_interactivity;
}

static void layer_surface_get_popup(struct wl_client* client, struct wl_resource* resource,
                                    struct wl_resource* popup) {
    (void)client;
    (void)resource;
    (void)popup;
}

static void layer_surface_ack_configure(struct wl_client* client, struct wl_resource* resource,
                                        uint32_t serial) {
    (void)client;
    (void)serial;
    Owl_Layer_Surface* surface = wl_resource_get_user_data(resource);
    if (!surface) return;
}

static void layer_surface_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void layer_surface_set_layer(struct wl_client* client, struct wl_resource* resource,
                                    uint32_t layer) {
    (void)client;
    Owl_Layer_Surface* surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->layer = layer;
}

static void layer_surface_set_exclusive_edge(struct wl_client* client, struct wl_resource* resource,
                                             uint32_t edge) {
    (void)client;
    (void)resource;
    (void)edge;
}

static const struct zwlr_layer_surface_v1_interface layer_surface_interface = {
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

static void layer_surface_destroy_handler(struct wl_resource* resource) {
    Owl_Layer_Surface* surface = wl_resource_get_user_data(resource);
    if (!surface) return;

    if (surface->mapped) {
        surface->mapped = false;
        owl_invoke_layer_surface_callback(surface->display, OWL_LAYER_SURFACE_EVENT_UNMAP, surface);
    }

    owl_invoke_layer_surface_callback(surface->display, OWL_LAYER_SURFACE_EVENT_DESTROY, surface);

    wl_list_remove(&surface->link);
    surface->display->layer_surface_count--;

    free(surface->namespace);
    free(surface);
}

/**
 * owl_layer_surface_send_configure() - Send a configure event to a layer surface
 * @ls: the layer surface to configure
 * @width: the width to suggest
 * @height: the height to suggest
 *
 * Calculates the appropriate size for a layer surface based on its anchor
 * points and the output dimensions, then sends a configure event to the client.
 */
static void owl_layer_surface_send_configure(Owl_Layer_Surface* ls, uint32_t width, uint32_t height) {
    if (!ls || !ls->layer_surface_resource) return;

    Owl_Output* output = ls->output;
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

    zwlr_layer_surface_v1_send_configure(ls->layer_surface_resource,
                                         layer_surface_configure_serial++, width, height);
}

static void layer_shell_get_layer_surface(struct wl_client* client, struct wl_resource* resource,
                                          uint32_t id, struct wl_resource* surface_resource,
                                          struct wl_resource* output_resource, uint32_t layer,
                                          const char* namespace) {
    Owl_Display* display = wl_resource_get_user_data(resource);
    Owl_Surface* wl_surface = owl_surface_from_resource(surface_resource);

    if (!wl_surface) {
        wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_ROLE,
                               "surface is null");
        return;
    }

    if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
        wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
                               "invalid layer %d", layer);
        return;
    }

    Owl_Layer_Surface* ls = calloc(1, sizeof(Owl_Layer_Surface));
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
    ls->layer_surface_resource = wl_resource_create(client, &zwlr_layer_surface_v1_interface,
                                                    version, id);
    if (!ls->layer_surface_resource) {
        free(ls->namespace);
        free(ls);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(ls->layer_surface_resource, &layer_surface_interface,
                                   ls, layer_surface_destroy_handler);

    wl_list_insert(&display->layer_surfaces, &ls->link);
    display->layer_surface_count++;

    owl_invoke_layer_surface_callback(display, OWL_LAYER_SURFACE_EVENT_CREATE, ls);

    owl_layer_surface_send_configure(ls, ls->width, ls->height);

    fprintf(stderr, "owl: layer_surface created (layer: %d, namespace: %s)\n",
            layer, namespace ? namespace : "(null)");
}

static void layer_shell_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_interface = {
    .get_layer_surface = layer_shell_get_layer_surface,
    .destroy = layer_shell_destroy,
};

static void layer_shell_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    Owl_Display* display = data;

    uint32_t bound_version = version < 4 ? version : 4;
    struct wl_resource* resource = wl_resource_create(client, &zwlr_layer_shell_v1_interface,
                                                      bound_version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &layer_shell_interface, display, NULL);
}

void owl_layer_shell_init(Owl_Display* display) {
    display->layer_shell_global = wl_global_create(display->wayland_display,
        &zwlr_layer_shell_v1_interface, 4, display, layer_shell_bind);

    if (!display->layer_shell_global) {
        fprintf(stderr, "owl: failed to create layer_shell global\n");
        return;
    }

    fprintf(stderr, "owl: layer-shell initialized\n");
}

void owl_layer_shell_cleanup(Owl_Display* display) {
    if (display->layer_shell_global) {
        wl_global_destroy(display->layer_shell_global);
        display->layer_shell_global = NULL;
    }
}

Owl_Layer_Surface** owl_get_layer_surfaces(Owl_Display* display, int* count) {
    if (!display || !count) return NULL;

    *count = display->layer_surface_count;
    if (*count == 0) return NULL;

    Owl_Layer_Surface** surfaces = calloc(*count, sizeof(Owl_Layer_Surface*));
    if (!surfaces) return NULL;

    int i = 0;
    Owl_Layer_Surface* ls;
    wl_list_for_each(ls, &display->layer_surfaces, link) {
        surfaces[i++] = ls;
        if (i >= *count) break;
    }

    return surfaces;
}

Owl_Layer owl_layer_surface_get_layer(Owl_Layer_Surface* surface) {
    return surface ? surface->layer : OWL_LAYER_BACKGROUND;
}

uint32_t owl_layer_surface_get_anchor(Owl_Layer_Surface* surface) {
    return surface ? surface->anchor : 0;
}

int owl_layer_surface_get_exclusive_zone(Owl_Layer_Surface* surface) {
    return surface ? surface->exclusive_zone : 0;
}

int owl_layer_surface_get_margin_top(Owl_Layer_Surface* surface) {
    return surface ? surface->margin_top : 0;
}

int owl_layer_surface_get_margin_right(Owl_Layer_Surface* surface) {
    return surface ? surface->margin_right : 0;
}

int owl_layer_surface_get_margin_bottom(Owl_Layer_Surface* surface) {
    return surface ? surface->margin_bottom : 0;
}

int owl_layer_surface_get_margin_left(Owl_Layer_Surface* surface) {
    return surface ? surface->margin_left : 0;
}

Owl_Keyboard_Interactivity owl_layer_surface_get_keyboard_interactivity(Owl_Layer_Surface* surface) {
    return surface ? surface->keyboard_interactivity : OWL_KEYBOARD_INTERACTIVITY_NONE;
}

int owl_layer_surface_get_width(Owl_Layer_Surface* surface) {
    return surface ? surface->configured_width : 0;
}

int owl_layer_surface_get_height(Owl_Layer_Surface* surface) {
    return surface ? surface->configured_height : 0;
}

const char* owl_layer_surface_get_namespace(Owl_Layer_Surface* surface) {
    return surface ? surface->namespace : NULL;
}

bool owl_layer_surface_is_mapped(Owl_Layer_Surface* surface) {
    return surface ? surface->mapped : false;
}
