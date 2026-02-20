#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

static FILE* disp_log = NULL;
static void disp_debug(const char* fmt, ...) {
    if (!disp_log) disp_log = fopen("/tmp/owl_display.log", "w");
    if (disp_log) {
        va_list args;
        va_start(args, fmt);
        vfprintf(disp_log, fmt, args);
        va_end(args);
        fflush(disp_log);
    }
}

static void client_destroyed(struct wl_listener* listener, void* data) {
    (void)data;
    disp_debug("client destroyed\n");
    wl_list_remove(&listener->link);
    free(listener);
}

static void client_created(struct wl_listener* listener, void* data) {
    (void)listener;
    struct wl_client* client = data;
    disp_debug("client connected: %p\n", (void*)client);

    struct wl_listener* destroy_listener = malloc(sizeof(struct wl_listener));
    if (destroy_listener) {
        destroy_listener->notify = client_destroyed;
        wl_client_add_destroy_listener(client, destroy_listener);
    }
}

static struct wl_listener client_created_listener = {
    .notify = client_created,
};

static int open_drm_device(void) {
    const char* drm_paths[] = {
        "/dev/dri/card0",
        "/dev/dri/card1",
        NULL
    };

    for (int index = 0; drm_paths[index] != NULL; index++) {
        int fd = open(drm_paths[index], O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

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

static bool init_gbm(Owl_Display* display) {
    display->gbm_device = gbm_create_device(display->drm_fd);
    if (!display->gbm_device) {
        fprintf(stderr, "owl: failed to create GBM device\n");
        return false;
    }
    return true;
}

static PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display_ext = NULL;

static bool init_egl(Owl_Display* display) {
    get_platform_display_ext = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (get_platform_display_ext) {
        display->egl_display = get_platform_display_ext(
            EGL_PLATFORM_GBM_KHR, display->gbm_device, NULL);
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
    if (!eglChooseConfig(display->egl_display, config_attribs,
                         &display->egl_config, 1, &num_configs) || num_configs < 1) {
        fprintf(stderr, "owl: failed to choose EGL config\n");
        return false;
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    display->egl_context = eglCreateContext(
        display->egl_display, display->egl_config, EGL_NO_CONTEXT, context_attribs);

    if (display->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "owl: failed to create EGL context\n");
        return false;
    }

    return true;
}

static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
                              unsigned int tv_usec, void* user_data) {
    (void)fd;
    (void)sequence;
    (void)tv_sec;
    (void)tv_usec;
    Owl_Output* output = user_data;
    disp_debug("page_flip_handler: output=%p\n", (void*)output);
    if (output) {
        output->page_flip_pending = false;
        if (output->current_bo) {
            gbm_surface_release_buffer(output->gbm_surface, output->current_bo);
        }
        output->current_bo = output->next_bo;
        output->next_bo = NULL;

        if (output->display) {
            disp_debug("page_flip_handler: triggering re-render\n");
            owl_render_frame(output->display, output);
        }
    }
}

static int handle_drm_event(int fd, uint32_t mask, void* data) {
    (void)mask;
    (void)data;
    disp_debug("handle_drm_event called\n");
    drmEventContext event_context = {
        .version = 2,
        .page_flip_handler = page_flip_handler,
    };
    drmHandleEvent(fd, &event_context);
    disp_debug("handle_drm_event done\n");

    return 0;
}

Owl_Display* owl_display_create(void) {
    Owl_Display* display = calloc(1, sizeof(Owl_Display));
    if (!display) {
        return NULL;
    }

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

    display->drm_event_source = wl_event_loop_add_fd(
        display->event_loop, display->drm_fd,
        WL_EVENT_READABLE, handle_drm_event, display);

    owl_output_init(display);
    owl_input_init(display);
    owl_seat_init(display);
    owl_surface_init(display);
    owl_xdg_shell_init(display);
    owl_render_init(display);

    wl_display_add_client_created_listener(display->wayland_display, &client_created_listener);

    display->running = false;

    return display;
}

void owl_display_destroy(Owl_Display* display) {
    if (!display) {
        return;
    }

    owl_render_cleanup(display);
    owl_xdg_shell_cleanup(display);
    owl_surface_cleanup(display);
    owl_seat_cleanup(display);
    owl_input_cleanup(display);
    owl_output_cleanup(display);

    if (display->drm_event_source) {
        wl_event_source_remove(display->drm_event_source);
    }

    if (display->egl_context) {
        eglDestroyContext(display->egl_display, display->egl_context);
    }

    if (display->egl_display) {
        eglTerminate(display->egl_display);
    }

    if (display->gbm_device) {
        gbm_device_destroy(display->gbm_device);
    }

    if (display->drm_fd >= 0) {
        drmDropMaster(display->drm_fd);
        close(display->drm_fd);
    }

    if (display->wayland_display) {
        wl_display_destroy(display->wayland_display);
    }

    free(display);
}

void owl_display_run(Owl_Display* display) {
    if (!display) {
        return;
    }

    display->running = true;

    for (int index = 0; index < display->output_count; index++) {
        owl_render_frame(display, display->outputs[index]);
    }

    while (display->running) {
        wl_display_flush_clients(display->wayland_display);
        wl_event_loop_dispatch(display->event_loop, -1);
    }
}

void owl_display_terminate(Owl_Display* display) {
    if (!display) {
        return;
    }

    display->running = false;
}

const char* owl_display_get_socket_name(Owl_Display* display) {
    if (!display) {
        return NULL;
    }
    return display->socket_name;
}

int owl_display_get_pointer_x(Owl_Display* display) {
    return display ? (int)display->pointer_x : 0;
}

int owl_display_get_pointer_y(Owl_Display* display) {
    return display ? (int)display->pointer_y : 0;
}
