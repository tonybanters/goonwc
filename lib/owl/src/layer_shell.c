#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.c"

static uint32_t layer_surface_configure_serial = 1;

static void layer_surface_set_size(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t width,
        uint32_t height
    ) {
    (void)client;
    owl_layer_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->width = width;
    surface->height = height;
}

static void layer_surface_set_anchor(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t anchor
    ) {
    (void)client;
    owl_layer_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->anchor = anchor;
}

static void layer_surface_set_exclusive_zone(
        struct wl_client *client,
        struct wl_resource *resource,
        int32_t zone
    ) {
    (void)client;
    owl_layer_surface* surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->exclusive_zone = zone;
}

static void layer_surface_set_margin(
        struct wl_client *client,
        struct wl_resource *resource,
        int32_t top,
        int32_t right,
        int32_t bottom,
        int32_t left
    ) {
    (void)client;
    owl_layer_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->margin_top = top;
    surface->margin_right = right;
    surface->margin_bottom = bottom;
    surface->margin_left = left;
}

static void layer_surface_set_keyboard_interactivity(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t keyboard_interactivity
    ) {
    (void)client;
    owl_layer_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->keyboard_interactivity = keyboard_interactivity;
}

static void layer_surface_get_popup(
        struct wl_client *client,
        struct wl_resource *resource,
        struct wl_resource *popup
    ) {
    (void)client;
    (void)resource;
    (void)popup;
}

static void layer_surface_ack_configure(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t serial
    ) {
    (void)client;
    (void)serial;
    owl_layer_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
}

static void layer_surface_destroy(struct wl_client *client, struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void layer_surface_set_layer(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t layer
    ) {
    (void)client;
    owl_layer_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->layer = layer;
}

static void layer_surface_set_exclusive_edge(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t edge
    ) {
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
    owl_layer_surface* surface = wl_resource_get_user_data(resource);
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

/**
 * owl_layer_surface_send_configure() - Send a configure event to a layer surface
 * @ls: the layer surface to configure
 * @width: the width to suggest
 * @height: the height to suggest
 *
 * Calculates the appropriate size for a layer surface based on its anchor
 * points and the output dimensions, then sends a configure event to the client.
 */
static void owl_layer_surface_send_configure(owl_layer_surface* ls, uint32_t width, uint32_t height) {
    if (!ls || !ls->layer_surface_resource) return;

    owl_output* output = ls->output;
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

static void layer_shell_get_layer_surface(
        struct wl_client *client,
        struct wl_resource *resource,
        uint32_t id,
        struct wl_resource *surface_resource,
        struct wl_resource *output_resource,
        uint32_t layer,
        const char *namespace
    ) {
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
    ls->layer_surface_resource = wl_resource_create(client, &zwlr_layer_surface_v1_interface,
                                                    version, id);
    if (!ls->layer_surface_resource) {
        free(ls->namespace);
        free(ls);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(ls->layer_surface_resource, &layer_surface_interface, ls, layer_surface_destroy_handler);

    if (display->layer_surface_count < OWL_MAX_LAYER_SURFACES) {
        display->layer_surfaces[display->layer_surface_count++] = ls;
    }

    owl_invoke_layer_surface_callback(display, OWL_LAYER_SURFACE_EVENT_CREATE, ls);

    /* Don't send configure yet - wait for first commit when properties are set */
    ls->initial_configure_sent = false;

    fprintf(stderr, "owl: layer_surface created (layer: %d, namespace: %s)\n", layer, namespace ? namespace : "(null)");
}

static void layer_shell_destroy(struct wl_client *client, struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_interface = {
    .get_layer_surface = layer_shell_get_layer_surface,
    .destroy = layer_shell_destroy,
};

static void layer_shell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    owl_display* display = data;

    uint32_t bound_version = version < 4 ? version : 4;
    struct wl_resource* resource = wl_resource_create(client, &zwlr_layer_shell_v1_interface, bound_version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &layer_shell_interface, display, NULL);
}

void owl_layer_shell_init(owl_display *display) {
    display->layer_shell_global = wl_global_create(
        display->wayland_display,
        &zwlr_layer_shell_v1_interface,
        4,
        display,
        layer_shell_bind
    );

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

void owl_layer_surface_send_initial_configure(owl_layer_surface *layer_surface) {
    if (!layer_surface || layer_surface->initial_configure_sent) return;
    layer_surface->initial_configure_sent = true;
    owl_layer_surface_send_configure(layer_surface, layer_surface->width, layer_surface->height);
}

owl_layer_surface **owl_display_get_layer_surfaces(owl_display *display, int *count) {
    if (!display || !count) {
        if (count) *count = 0;
        return NULL;
    }
    *count = display->layer_surface_count;
    return display->layer_surfaces;
}

