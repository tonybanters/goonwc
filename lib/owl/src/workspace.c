#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wayland-server-protocol.h>
#include "ext-workspace-v1-protocol.h"
#include "ext-workspace-v1-protocol.c"

typedef struct {
    struct wl_resource* resource;
    owl_workspace* workspace;
    struct wl_list link;
} workspace_resource;

static void workspace_handle_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void workspace_handle_activate(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    workspace_resource* ws_res = wl_resource_get_user_data(resource);
    if (ws_res && ws_res->workspace && ws_res->workspace->display) {
        owl_invoke_workspace_callback(ws_res->workspace->display, OWL_WORKSPACE_EVENT_ACTIVATE, ws_res->workspace);
    }
}

static void workspace_handle_deactivate(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    workspace_resource* ws_res = wl_resource_get_user_data(resource);
    if (ws_res && ws_res->workspace && ws_res->workspace->display) {
        owl_invoke_workspace_callback(ws_res->workspace->display, OWL_WORKSPACE_EVENT_DEACTIVATE, ws_res->workspace);
    }
}

static void workspace_handle_assign(struct wl_client* client, struct wl_resource* resource,
                                     struct wl_resource* workspace_group) {
    (void)client;
    (void)resource;
    (void)workspace_group;
}

static void workspace_handle_remove(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    workspace_resource* ws_res = wl_resource_get_user_data(resource);
    if (ws_res && ws_res->workspace && ws_res->workspace->display) {
        owl_invoke_workspace_callback(ws_res->workspace->display, OWL_WORKSPACE_EVENT_REMOVE, ws_res->workspace);
    }
}

static const struct ext_workspace_handle_v1_interface workspace_interface = {
    .destroy = workspace_handle_destroy,
    .activate = workspace_handle_activate,
    .deactivate = workspace_handle_deactivate,
    .assign = workspace_handle_assign,
    .remove = workspace_handle_remove,
};

static void workspace_resource_destroy(struct wl_resource* resource) {
    workspace_resource* ws_resource = wl_resource_get_user_data(resource);
    if (ws_resource) {
        wl_list_remove(&ws_resource->link);
        free(ws_resource);
    }
}

static void workspace_group_handle_create_workspace(struct wl_client* client,
                                                     struct wl_resource* resource,
                                                     const char* name) {
    (void)client;
    (void)resource;
    (void)name;
}

static void workspace_group_handle_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static const struct ext_workspace_group_handle_v1_interface workspace_group_interface = {
    .create_workspace = workspace_group_handle_create_workspace,
    .destroy = workspace_group_handle_destroy,
};

static void workspace_group_resource_destroy(struct wl_resource* resource) {
    workspace_resource* ws_resource = wl_resource_get_user_data(resource);
    if (ws_resource) {
        wl_list_remove(&ws_resource->link);
        free(ws_resource);
    }
}

static void manager_handle_commit(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    (void)resource;
}

static void manager_handle_stop(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    ext_workspace_manager_v1_send_finished(resource);
}

static const struct ext_workspace_manager_v1_interface manager_interface = {
    .commit = manager_handle_commit,
    .stop = manager_handle_stop,
};

static void manager_resource_destroy(struct wl_resource* resource) {
    workspace_resource* ws_resource = wl_resource_get_user_data(resource);
    if (ws_resource) {
        wl_list_remove(&ws_resource->link);
        free(ws_resource);
    }
}

static void send_workspace_to_client(owl_display* display, owl_workspace* workspace,
                                      struct wl_resource* manager_resource,
                                      struct wl_resource* group_resource) {
    struct wl_client* client = wl_resource_get_client(manager_resource);
    uint32_t version = wl_resource_get_version(manager_resource);

    struct wl_resource* ws_resource = wl_resource_create(client,
        &ext_workspace_handle_v1_interface, version, 0);
    if (!ws_resource) {
        return;
    }

    workspace_resource* ws_res = calloc(1, sizeof(workspace_resource));
    if (!ws_res) {
        wl_resource_destroy(ws_resource);
        return;
    }
    ws_res->resource = ws_resource;
    ws_res->workspace = workspace;
    wl_list_insert(&workspace->resources, &ws_res->link);

    wl_resource_set_implementation(ws_resource, &workspace_interface, ws_res,
                                    workspace_resource_destroy);

    ext_workspace_manager_v1_send_workspace(manager_resource, ws_resource);

    if (workspace->id) {
        ext_workspace_handle_v1_send_id(ws_resource, workspace->id);
    }
    ext_workspace_handle_v1_send_name(ws_resource, workspace->name);

    struct wl_array coords;
    wl_array_init(&coords);
    int32_t* coord = wl_array_add(&coords, sizeof(int32_t));
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

static void workspace_manager_bind(struct wl_client* client, void* data,
                                    uint32_t version, uint32_t id) {
    owl_display* display = data;
    uint32_t bound_version = version < 1 ? version : 1;

    struct wl_resource* resource = wl_resource_create(client,
        &ext_workspace_manager_v1_interface, bound_version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    workspace_resource* ws_resource = calloc(1, sizeof(workspace_resource));
    if (!ws_resource) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    ws_resource->resource = resource;
    ws_resource->workspace = NULL;
    wl_list_insert(&display->workspace_manager_resources, &ws_resource->link);

    wl_resource_set_implementation(resource, &manager_interface, ws_resource,
                                    manager_resource_destroy);

    struct wl_resource* group_resource = wl_resource_create(client,
        &ext_workspace_group_handle_v1_interface, bound_version, 0);
    if (!group_resource) {
        return;
    }

    workspace_resource* group_res = calloc(1, sizeof(workspace_resource));
    if (!group_res) {
        wl_resource_destroy(group_resource);
        return;
    }
    group_res->resource = group_resource;
    group_res->workspace = NULL;
    wl_list_insert(&display->workspace_group_resources, &group_res->link);

    wl_resource_set_implementation(group_resource, &workspace_group_interface, group_res,
                                    workspace_group_resource_destroy);

    ext_workspace_manager_v1_send_workspace_group(resource, group_resource);

    uint32_t group_capabilities = 0;
    ext_workspace_group_handle_v1_send_capabilities(group_resource, group_capabilities);

    owl_workspace* workspace;
    wl_list_for_each(workspace, &display->workspaces, link) {
        send_workspace_to_client(display, workspace, resource, group_resource);
    }

    ext_workspace_manager_v1_send_done(resource);
}

void owl_workspace_init(owl_display* display) {
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

void owl_workspace_cleanup(owl_display* display) {
    owl_workspace* workspace;
    owl_workspace* tmp;
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

owl_workspace* owl_workspace_create(owl_display* display, const char* name) {
    owl_workspace* workspace = calloc(1, sizeof(owl_workspace));
    if (!workspace) {
        return NULL;
    }

    workspace->display = display;
    workspace->name = strdup(name);
    workspace->id = strdup(name);
    workspace->state = 0;
    workspace->coordinate = display->workspace_count;
    wl_list_init(&workspace->resources);
    wl_list_insert(&display->workspaces, &workspace->link);
    display->workspace_count++;

    workspace_resource* mgr_res;
    wl_list_for_each(mgr_res, &display->workspace_manager_resources, link) {
        workspace_resource* grp_res;
        wl_list_for_each(grp_res, &display->workspace_group_resources, link) {
            if (wl_resource_get_client(mgr_res->resource) ==
                wl_resource_get_client(grp_res->resource)) {
                send_workspace_to_client(display, workspace, mgr_res->resource,
                                          grp_res->resource);
                ext_workspace_manager_v1_send_done(mgr_res->resource);
                break;
            }
        }
    }

    return workspace;
}

void owl_workspace_destroy(owl_workspace* workspace) {
    if (!workspace) {
        return;
    }

    workspace_resource* ws_res;
    workspace_resource* tmp;
    wl_list_for_each_safe(ws_res, tmp, &workspace->resources, link) {
        ext_workspace_handle_v1_send_removed(ws_res->resource);
    }

    workspace_resource* mgr_res;
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

void owl_workspace_set_state(owl_workspace* workspace, uint32_t state) {
    if (!workspace || workspace->state == state) {
        return;
    }

    workspace->state = state;

    workspace_resource* ws_res;
    wl_list_for_each(ws_res, &workspace->resources, link) {
        ext_workspace_handle_v1_send_state(ws_res->resource, state);
    }
}

void owl_workspace_set_coordinates(owl_workspace* workspace, int32_t x) {
    if (!workspace) {
        return;
    }

    workspace->coordinate = x;

    workspace_resource* ws_res;
    wl_list_for_each(ws_res, &workspace->resources, link) {
        struct wl_array coords;
        wl_array_init(&coords);
        int32_t* coord = wl_array_add(&coords, sizeof(int32_t));
        if (coord) {
            *coord = x;
            ext_workspace_handle_v1_send_coordinates(ws_res->resource, &coords);
        }
        wl_array_release(&coords);
    }
}

void owl_workspace_commit(owl_display* display) {
    if (!display) {
        return;
    }

    workspace_resource* mgr_res;
    wl_list_for_each(mgr_res, &display->workspace_manager_resources, link) {
        ext_workspace_manager_v1_send_done(mgr_res->resource);
    }
}

void owl_set_workspace_callback(owl_display* display, owl_workspace_event type,
                                 owl_workspace_callback callback, void* data) {
    if (!display || type < 0 || type >= 3) {
        return;
    }

    int count = display->workspace_callback_count[type];
    if (count >= OWL_MAX_CALLBACKS) {
        return;
    }

    display->workspace_callbacks[type][count].callback = callback;
    display->workspace_callbacks[type][count].data = data;
    display->workspace_callback_count[type]++;
}

void owl_invoke_workspace_callback(owl_display* display, owl_workspace_event type,
                                    owl_workspace* workspace) {
    if (!display || type < 0 || type >= 3) {
        return;
    }

    int count = display->workspace_callback_count[type];
    for (int i = 0; i < count; i++) {
        workspace_callback_entry* entry = &display->workspace_callbacks[type][i];
        if (entry->callback) {
            entry->callback(display, workspace, entry->data);
        }
    }
}
