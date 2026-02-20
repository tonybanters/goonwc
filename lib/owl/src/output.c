#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <wayland-server-protocol.h>

static void wl_output_release(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_output_interface output_interface = {
    .release = wl_output_release,
};

static void wl_output_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    Owl_Output* output = data;
    uint32_t bound_version = version < 4 ? version : 4;

    struct wl_resource* resource = wl_resource_create(client, &wl_output_interface, bound_version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &output_interface, output, NULL);

    wl_output_send_geometry(resource,
        output->pos_x, output->pos_y,
        0, 0,
        WL_OUTPUT_SUBPIXEL_UNKNOWN,
        "owl", output->name,
        WL_OUTPUT_TRANSFORM_NORMAL);

    wl_output_send_mode(resource,
        WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
        output->width, output->height,
        output->drm_mode.vrefresh * 1000);

    if (bound_version >= 2) {
        wl_output_send_scale(resource, 1);
    }

    if (bound_version >= 2) {
        wl_output_send_done(resource);
    }
}

static Owl_Output* create_output(Owl_Display* display, drmModeConnector* connector,
                                  drmModeCrtc* crtc) {
    Owl_Output* output = calloc(1, sizeof(Owl_Output));
    if (!output) {
        return NULL;
    }

    output->display = display;
    output->drm_connector_id = connector->connector_id;
    output->drm_crtc_id = crtc->crtc_id;
    output->drm_mode = connector->modes[0];
    output->width = output->drm_mode.hdisplay;
    output->height = output->drm_mode.vdisplay;
    output->pos_x = crtc->x;
    output->pos_y = crtc->y;

    const char* connector_types[] = {
        "Unknown", "VGA", "DVII", "DVID", "DVIA", "Composite", "SVIDEO",
        "LVDS", "Component", "9PinDIN", "DisplayPort", "HDMIA", "HDMIB",
        "TV", "eDP", "VIRTUAL", "DSI", "DPI"
    };

    const char* type_name = "Unknown";
    if (connector->connector_type < sizeof(connector_types) / sizeof(connector_types[0])) {
        type_name = connector_types[connector->connector_type];
    }

    char name_buffer[64];
    snprintf(name_buffer, sizeof(name_buffer), "%s-%d",
             type_name, connector->connector_type_id);
    output->name = strdup(name_buffer);

    output->gbm_surface = gbm_surface_create(
        display->gbm_device,
        output->width, output->height,
        GBM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

    if (!output->gbm_surface) {
        fprintf(stderr, "owl: failed to create GBM surface for %s\n", output->name);
        free(output->name);
        free(output);
        return NULL;
    }

    output->egl_surface = eglCreateWindowSurface(
        display->egl_display, display->egl_config,
        (EGLNativeWindowType)output->gbm_surface, NULL);

    if (output->egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "owl: failed to create EGL surface for %s\n", output->name);
        gbm_surface_destroy(output->gbm_surface);
        free(output->name);
        free(output);
        return NULL;
    }

    output->wl_output_global = wl_global_create(display->wayland_display,
        &wl_output_interface, 4, output, wl_output_bind);

    if (!output->wl_output_global) {
        fprintf(stderr, "owl: failed to create wl_output global for %s\n", output->name);
        eglDestroySurface(display->egl_display, output->egl_surface);
        gbm_surface_destroy(output->gbm_surface);
        free(output->name);
        free(output);
        return NULL;
    }

    fprintf(stderr, "owl: output %s: %dx%d\n", output->name, output->width, output->height);

    return output;
}

static void destroy_output(Owl_Output* output) {
    if (!output) {
        return;
    }

    if (output->wl_output_global) {
        wl_global_destroy(output->wl_output_global);
    }

    if (output->current_bo) {
        gbm_surface_release_buffer(output->gbm_surface, output->current_bo);
    }

    if (output->egl_surface) {
        eglDestroySurface(output->display->egl_display, output->egl_surface);
    }

    if (output->gbm_surface) {
        gbm_surface_destroy(output->gbm_surface);
    }

    free(output->name);
    free(output);
}

void owl_output_init(Owl_Display* display) {
    drmModeRes* resources = drmModeGetResources(display->drm_fd);
    if (!resources) {
        fprintf(stderr, "owl: failed to get DRM resources\n");
        return;
    }

    for (int connector_index = 0; connector_index < resources->count_connectors; connector_index++) {
        drmModeConnector* connector = drmModeGetConnector(
            display->drm_fd, resources->connectors[connector_index]);

        if (!connector) {
            continue;
        }

        if (connector->connection != DRM_MODE_CONNECTED || connector->count_modes == 0) {
            drmModeFreeConnector(connector);
            continue;
        }

        drmModeEncoder* encoder = NULL;
        if (connector->encoder_id) {
            encoder = drmModeGetEncoder(display->drm_fd, connector->encoder_id);
        }

        if (!encoder) {
            for (int encoder_index = 0; encoder_index < connector->count_encoders; encoder_index++) {
                encoder = drmModeGetEncoder(display->drm_fd, connector->encoders[encoder_index]);
                if (encoder) {
                    break;
                }
            }
        }

        if (!encoder) {
            drmModeFreeConnector(connector);
            continue;
        }

        drmModeCrtc* crtc = NULL;
        if (encoder->crtc_id) {
            crtc = drmModeGetCrtc(display->drm_fd, encoder->crtc_id);
        }

        if (!crtc) {
            for (int crtc_index = 0; crtc_index < resources->count_crtcs; crtc_index++) {
                if (encoder->possible_crtcs & (1 << crtc_index)) {
                    crtc = drmModeGetCrtc(display->drm_fd, resources->crtcs[crtc_index]);
                    if (crtc) {
                        break;
                    }
                }
            }
        }

        if (!crtc) {
            drmModeFreeEncoder(encoder);
            drmModeFreeConnector(connector);
            continue;
        }

        if (!crtc->mode_valid) {
            drmModeSetCrtc(display->drm_fd, crtc->crtc_id, -1,
                          0, 0, &connector->connector_id, 1, &connector->modes[0]);
            drmModeFreeCrtc(crtc);
            crtc = drmModeGetCrtc(display->drm_fd, encoder->crtc_id ? encoder->crtc_id : resources->crtcs[0]);
        }

        if (display->output_count < OWL_MAX_OUTPUTS) {
            Owl_Output* output = create_output(display, connector, crtc);
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

void owl_output_cleanup(Owl_Display* display) {
    for (int index = 0; index < display->output_count; index++) {
        owl_invoke_output_callback(display, OWL_OUTPUT_EVENT_DISCONNECT, display->outputs[index]);
        destroy_output(display->outputs[index]);
        display->outputs[index] = NULL;
    }
    display->output_count = 0;
}

Owl_Output** owl_get_outputs(Owl_Display* display, int* count) {
    if (!display || !count) {
        if (count) *count = 0;
        return NULL;
    }
    *count = display->output_count;
    return display->outputs;
}

int owl_output_get_x(Owl_Output* output) {
    return output ? output->pos_x : 0;
}

int owl_output_get_y(Owl_Output* output) {
    return output ? output->pos_y : 0;
}

int owl_output_get_width(Owl_Output* output) {
    return output ? output->width : 0;
}

int owl_output_get_height(Owl_Output* output) {
    return output ? output->height : 0;
}

const char* owl_output_get_name(Owl_Output* output) {
    return output ? output->name : NULL;
}
