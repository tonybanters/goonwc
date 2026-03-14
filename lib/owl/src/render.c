#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wayland-server-protocol.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

#ifndef GL_UNPACK_ROW_LENGTH_EXT
#define GL_UNPACK_ROW_LENGTH_EXT 0x0CF2
#endif


static const char* vertex_shader_source =
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "uniform vec2 screen_size;\n"
    "uniform vec2 surface_pos;\n"
    "uniform vec2 surface_size;\n"
    "void main() {\n"
    "    vec2 pos = position * surface_size + surface_pos;\n"
    "    vec2 normalized = (pos / screen_size) * 2.0 - 1.0;\n"
    "    normalized.y = -normalized.y;\n"
    "    gl_Position = vec4(normalized, 0.0, 1.0);\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

static const char* fragment_shader_source =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture0;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(texture0, v_texcoord);\n"
    "}\n";

static GLuint shader_program = 0;
static GLint attr_position = -1;
static GLint attr_texcoord = -1;
static GLint uniform_screen_size = -1;
static GLint uniform_surface_pos = -1;
static GLint uniform_surface_size = -1;
static GLint uniform_texture = -1;

static GLuint quad_vbo = 0;

static float quad_vertices[] = {
    0.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f,
};

static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "owl: shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static bool init_shaders(void) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    if (!vertex_shader) {
        return false;
    }

    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return false;
    }

    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint status;
    glGetProgramiv(shader_program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        glGetProgramInfoLog(shader_program, sizeof(log), NULL, log);
        fprintf(stderr, "owl: shader link error: %s\n", log);
        glDeleteProgram(shader_program);
        shader_program = 0;
        return false;
    }

    attr_position = glGetAttribLocation(shader_program, "position");
    attr_texcoord = glGetAttribLocation(shader_program, "texcoord");
    uniform_screen_size = glGetUniformLocation(shader_program, "screen_size");
    uniform_surface_pos = glGetUniformLocation(shader_program, "surface_pos");
    uniform_surface_size = glGetUniformLocation(shader_program, "surface_size");
    uniform_texture = glGetUniformLocation(shader_program, "texture0");

    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    fprintf(stderr, "owl: shaders initialized\n");
    return true;
}

static uint32_t get_framebuffer_for_bo(owl_display* display, struct gbm_bo* bo) {
    uint32_t* fb_id_ptr = gbm_bo_get_user_data(bo);
    if (fb_id_ptr) {
        return *fb_id_ptr;
    }

    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;

    uint32_t* fb_id = malloc(sizeof(uint32_t));
    if (!fb_id) {
        return 0;
    }

    int result = drmModeAddFB(display->drm_fd, width, height, 24, 32, stride, handle, fb_id);
    if (result) {
        fprintf(stderr, "owl: failed to add framebuffer: %d\n", result);
        free(fb_id);
        return 0;
    }

    gbm_bo_set_user_data(bo, fb_id, NULL);

    return *fb_id;
}

static uint32_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void owl_render_init(owl_display* display) {
    if (!eglMakeCurrent(display->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, display->egl_context)) {
        fprintf(stderr, "owl: failed to make EGL context current for init\n");
        return;
    }

    if (!init_shaders()) {
        fprintf(stderr, "owl: failed to initialize shaders\n");
    }
}

void owl_render_cleanup(owl_display* display) {
    (void)display;

    if (quad_vbo) {
        glDeleteBuffers(1, &quad_vbo);
        quad_vbo = 0;
    }

    if (shader_program) {
        glDeleteProgram(shader_program);
        shader_program = 0;
    }
}

uint32_t owl_render_upload_texture(owl_display* display, owl_surface* surface) {
    if (!surface || !surface->current.buffer) {
        return 0;
    }

    owl_shm_buffer* buffer = surface->current.buffer;
    owl_shm_pool* pool = buffer->pool;

    if (!pool || !pool->data) {
        return 0;
    }

    if (!eglMakeCurrent(display->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, display->egl_context)) {
        return 0;
    }

    if (surface->texture_id == 0) {
        glGenTextures(1, &surface->texture_id);
    }

    glBindTexture(GL_TEXTURE_2D, surface->texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    void* pixels = (char*)pool->data + buffer->offset;

    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, buffer->stride / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, buffer->width, buffer->height,
                 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

    surface->texture_width = buffer->width;
    surface->texture_height = buffer->height;

    glBindTexture(GL_TEXTURE_2D, 0);

    wl_buffer_send_release(buffer->resource);

    return surface->texture_id;
}

void owl_render_surface(owl_display* display, owl_surface* surface, int x, int y) {
    (void)display;

    if (!surface || surface->texture_id == 0) {
        return;
    }

    glUseProgram(shader_program);

    glUniform2f(uniform_surface_pos, (float)x, (float)y);
    glUniform2f(uniform_surface_size, (float)surface->texture_width, (float)surface->texture_height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, surface->texture_id);
    glUniform1i(uniform_texture, 0);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(attr_position);
    glEnableVertexAttribArray(attr_texcoord);
    glVertexAttribPointer(attr_position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(attr_texcoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(attr_position);
    glDisableVertexAttribArray(attr_texcoord);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/**
 * owl_layer_surface_get_position() - Calculate position for a layer surface
 * @ls: the layer surface
 * @output: the output to render on
 * @x: output parameter for x position
 * @y: output parameter for y position
 *
 * Calculates the position of a layer surface based on its anchor points,
 * margins, and the configured size.
 */
static void owl_layer_surface_get_position(owl_layer_surface* ls, owl_output* output, int* x, int* y) {
    int out_w = output->width;
    int out_h = output->height;
    int surf_w = ls->configured_width;
    int surf_h = ls->configured_height;

    if (ls->anchor & OWL_ANCHOR_LEFT) {
        *x = ls->margin_left;
    } else if (ls->anchor & OWL_ANCHOR_RIGHT) {
        *x = out_w - surf_w - ls->margin_right;
    } else {
        *x = (out_w - surf_w) / 2;
    }

    if (ls->anchor & OWL_ANCHOR_TOP) {
        *y = ls->margin_top;
    } else if (ls->anchor & OWL_ANCHOR_BOTTOM) {
        *y = out_h - surf_h - ls->margin_bottom;
    } else {
        *y = (out_h - surf_h) / 2;
    }
}

static void render_layer_surfaces(owl_display* display, owl_output* output, owl_layer layer) {
    for (int i = 0; i < display->layer_surface_count; i++) {
        owl_layer_surface* ls = display->layer_surfaces[i];
        if (ls->layer != layer || !ls->mapped || !ls->surface || !ls->surface->has_content) {
            continue;
        }
        int x, y;
        owl_layer_surface_get_position(ls, output, &x, &y);
        owl_render_surface(display, ls->surface, x, y);
    }
}

void owl_render_frame(owl_display* display, owl_output* output) {
    if (!display || !output) {
        return;
    }

    if (output->page_flip_pending) {
        return;
    }

    if (!eglMakeCurrent(display->egl_display, output->egl_surface,
                        output->egl_surface, display->egl_context)) {
        fprintf(stderr, "owl: failed to make EGL context current\n");
        return;
    }

    glViewport(0, 0, output->width, output->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shader_program);
    glUniform2f(uniform_screen_size, (float)output->width, (float)output->height);

    render_layer_surfaces(display, output, OWL_LAYER_BACKGROUND);
    render_layer_surfaces(display, output, OWL_LAYER_BOTTOM);

    owl_window* window;
    wl_list_for_each_reverse(window, &display->windows, link) {
        if (window->mapped && window->surface && window->surface->has_content) {
            owl_render_surface(display, window->surface, window->x, window->y);
        }
    }

    render_layer_surfaces(display, output, OWL_LAYER_TOP);
    render_layer_surfaces(display, output, OWL_LAYER_OVERLAY);

    if (display->cursor_surface && display->cursor_surface->has_content) {
        int cursor_x = (int)display->pointer_x - display->cursor_hotspot_x;
        int cursor_y = (int)display->pointer_y - display->cursor_hotspot_y;
        owl_render_surface(display, display->cursor_surface, cursor_x, cursor_y);
    }

    glDisable(GL_BLEND);

    if (!eglSwapBuffers(display->egl_display, output->egl_surface)) {
        fprintf(stderr, "owl: failed to swap buffers\n");
        return;
    }

    struct gbm_bo* bo = gbm_surface_lock_front_buffer(output->gbm_surface);
    if (!bo) {
        fprintf(stderr, "owl: failed to lock front buffer\n");
        return;
    }

    uint32_t fb_id = get_framebuffer_for_bo(display, bo);
    if (!fb_id) {
        gbm_surface_release_buffer(output->gbm_surface, bo);
        return;
    }

    if (!output->current_bo) {
        int result = drmModeSetCrtc(display->drm_fd, output->drm_crtc_id, fb_id,
                                    0, 0, &output->drm_connector_id, 1, (drmModeModeInfoPtr)output->drm_mode);
        if (result) {
            fprintf(stderr, "owl: failed to set CRTC: %d\n", result);
            gbm_surface_release_buffer(output->gbm_surface, bo);
            return;
        }
        output->current_bo = bo;

        owl_surface_send_frame_done(display, get_time_ms());
        return;
    }

    int result = drmModePageFlip(display->drm_fd, output->drm_crtc_id, fb_id,
                                  DRM_MODE_PAGE_FLIP_EVENT, output);
    if (result) {
        fprintf(stderr, "owl: page flip failed: %d\n", result);
        gbm_surface_release_buffer(output->gbm_surface, bo);
        return;
    }

    output->next_bo = bo;
    output->page_flip_pending = true;

    owl_surface_send_frame_done(display, get_time_ms());
}
