#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include "xdg-decoration-protocol.h"
#include "xdg-decoration-protocol.c"

static void decoration_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void decoration_set_mode(struct wl_client* client, struct wl_resource* resource, uint32_t mode) {
    (void)client;
    (void)mode;
    zxdg_toplevel_decoration_v1_send_configure(resource,
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void decoration_unset_mode(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    zxdg_toplevel_decoration_v1_send_configure(resource,
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_toplevel_decoration_v1_interface decoration_interface = {
    .destroy = decoration_destroy,
    .set_mode = decoration_set_mode,
    .unset_mode = decoration_unset_mode,
};

static void manager_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void manager_get_toplevel_decoration(struct wl_client* client,
                                            struct wl_resource* resource,
                                            uint32_t id,
                                            struct wl_resource* toplevel) {
    (void)toplevel;

    struct wl_resource* deco = wl_resource_create(client,
        &zxdg_toplevel_decoration_v1_interface, 1, id);
    if (!deco) {
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(deco, &decoration_interface, NULL, NULL);

    zxdg_toplevel_decoration_v1_send_configure(deco,
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_decoration_manager_v1_interface manager_interface = {
    .destroy = manager_destroy,
    .get_toplevel_decoration = manager_get_toplevel_decoration,
};

static void manager_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    (void)data;
    (void)version;

    struct wl_resource* resource = wl_resource_create(client,
        &zxdg_decoration_manager_v1_interface, 1, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &manager_interface, NULL, NULL);
}

static struct wl_global* decoration_global = NULL;

void owl_decoration_init(owl_display* display) {
    decoration_global = wl_global_create(display->wayland_display,
        &zxdg_decoration_manager_v1_interface, 1, display, manager_bind);

    if (!decoration_global) {
        fprintf(stderr, "owl: failed to create xdg-decoration global\n");
        return;
    }

    fprintf(stderr, "owl: xdg-decoration initialized\n");
}

void owl_decoration_cleanup(owl_display* display) {
    (void)display;

    if (decoration_global) {
        wl_global_destroy(decoration_global);
        decoration_global = NULL;
    }
}
