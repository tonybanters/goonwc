#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
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

static FILE* render_log = NULL;
static void render_debug(const char* fmt, ...) {
    if (!render_log) render_log = fopen("/tmp/owl_render.log", "w");
    if (render_log) {
        va_list args;
        va_start(args, fmt);
        vfprintf(render_log, fmt, args);
        va_end(args);
        fflush(render_log);
    }
}

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

static uint32_t get_framebuffer_for_bo(Owl_Display* display, struct gbm_bo* bo) {
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

void owl_render_init(Owl_Display* display) {
    if (!eglMakeCurrent(display->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, display->egl_context)) {
        fprintf(stderr, "owl: failed to make EGL context current for init\n");
        return;
    }

    if (!init_shaders()) {
        fprintf(stderr, "owl: failed to initialize shaders\n");
    }
}

void owl_render_cleanup(Owl_Display* display) {
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

uint32_t owl_render_upload_texture(Owl_Display* display, Owl_Surface* surface) {
    if (!surface || !surface->current.buffer) {
        return 0;
    }

    Owl_Shm_Buffer* buffer = surface->current.buffer;
    Owl_Shm_Pool* pool = buffer->pool;

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

void owl_render_surface(Owl_Display* display, Owl_Surface* surface, int x, int y) {
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

void owl_render_frame(Owl_Display* display, Owl_Output* output) {
    if (!display || !output) {
        render_debug("render_frame: null display or output\n");
        return;
    }

    if (output->page_flip_pending) {
        render_debug("render_frame: page_flip_pending, skipping\n");
        return;
    }
    render_debug("render_frame: starting\n");

    if (!eglMakeCurrent(display->egl_display, output->egl_surface,
                        output->egl_surface, display->egl_context)) {
        fprintf(stderr, "owl: failed to make EGL context current\n");
        return;
    }

    glViewport(0, 0, output->width, output->height);
    glClearColor(0.2f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shader_program);
    glUniform2f(uniform_screen_size, (float)output->width, (float)output->height);

    int window_count = 0;
    int rendered_count = 0;
    Owl_Window* window;
    wl_list_for_each_reverse(window, &display->windows, link) {
        window_count++;
        render_debug("  window %p: mapped=%d surface=%p has_content=%d\n",
                     (void*)window, window->mapped,
                     (void*)window->surface,
                     window->surface ? window->surface->has_content : 0);
        if (window->mapped && window->surface && window->surface->has_content) {
            render_debug("    rendering at %d,%d size=%dx%d\n",
                         window->pos_x, window->pos_y,
                         window->surface->texture_width, window->surface->texture_height);
            owl_render_surface(display, window->surface, window->pos_x, window->pos_y);
            rendered_count++;
        }
    }
    render_debug("render_frame: windows=%d rendered=%d\n", window_count, rendered_count);

    if (display->cursor_surface && display->cursor_surface->has_content) {
        int cursor_x = (int)display->pointer_x - display->cursor_hotspot_x;
        int cursor_y = (int)display->pointer_y - display->cursor_hotspot_y;
        owl_render_surface(display, display->cursor_surface, cursor_x, cursor_y);
        render_debug("render_frame: cursor at %d,%d\n", cursor_x, cursor_y);
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
                                    0, 0, &output->drm_connector_id, 1, &output->drm_mode);
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
