#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wayland-server-protocol.h>
#include "wlr-screencopy-unstable-v1-protocol.h"

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

#ifndef GL_UNPACK_ROW_LENGTH_EXT
#define GL_UNPACK_ROW_LENGTH_EXT 0x0CF2
#endif

#ifndef EGL_EXT_image_dma_buf_import
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#define EGL_DMA_BUF_PLANE1_FD_EXT 0x3275
#define EGL_DMA_BUF_PLANE1_OFFSET_EXT 0x3276
#define EGL_DMA_BUF_PLANE1_PITCH_EXT 0x3277
#define EGL_DMA_BUF_PLANE2_FD_EXT 0x3278
#define EGL_DMA_BUF_PLANE2_OFFSET_EXT 0x3279
#define EGL_DMA_BUF_PLANE2_PITCH_EXT 0x327A
#define EGL_DMA_BUF_PLANE3_FD_EXT 0x3440
#define EGL_DMA_BUF_PLANE3_OFFSET_EXT 0x3441
#define EGL_DMA_BUF_PLANE3_PITCH_EXT 0x3442
#endif

#ifndef EGL_EXT_image_dma_buf_import_modifiers
#define EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT 0x3443
#define EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT 0x3444
#define EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT 0x3445
#define EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT 0x3446
#define EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT 0x3447
#define EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT 0x3448
#define EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT 0x3449
#define EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT 0x344A
#endif

#ifndef GL_OES_EGL_image
#define GL_OES_EGL_image 1
typedef void *GLeglImageOES;
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum target, GLeglImageOES image);
#endif

typedef EGLImageKHR (*PFNEGLCREATEIMAGEKHRPROC)(EGLDisplay dpy, EGLContext ctx,
	EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list);
typedef EGLBoolean (*PFNEGLDESTROYIMAGEKHRPROC)(EGLDisplay dpy, EGLImageKHR image);
typedef EGLBoolean (*PFNEGLQUERYDMABUFFORMATSEXTPROC)(EGLDisplay dpy,
	EGLint max_formats, EGLint *formats, EGLint *num_formats);
typedef EGLBoolean (*PFNEGLQUERYDMABUFMODIFIERSEXTPROC)(EGLDisplay dpy,
	EGLint format, EGLint max_modifiers, EGLuint64KHR *modifiers,
	EGLBoolean *external_only, EGLint *num_modifiers);

static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
static PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT;
static PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT;

static bool dmabuf_import_supported = false;
static bool dmabuf_modifiers_supported = false;


static const char *vertex_shader_source =
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "uniform vec2 screen_size;\n"
    "uniform vec2 surface_pos;\n"
    "uniform vec2 surface_size;\n"
    "uniform vec4 src_rect;\n"
    "void main() {\n"
    "    vec2 pos = position * surface_size + surface_pos;\n"
    "    vec2 normalized = (pos / screen_size) * 2.0 - 1.0;\n"
    "    normalized.y = -normalized.y;\n"
    "    gl_Position = vec4(normalized, 0.0, 1.0);\n"
    "    v_texcoord = texcoord * src_rect.zw + src_rect.xy;\n"
    "}\n";

static const char *fragment_shader_source =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture0;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(texture0, v_texcoord);\n"
    "}\n";

static const char *solid_vertex_shader_source =
    "attribute vec2 position;\n"
    "uniform vec2 screen_size;\n"
    "uniform vec2 rect_pos;\n"
    "uniform vec2 rect_size;\n"
    "void main() {\n"
    "    vec2 pos = position * rect_size + rect_pos;\n"
    "    vec2 normalized = (pos / screen_size) * 2.0 - 1.0;\n"
    "    normalized.y = -normalized.y;\n"
    "    gl_Position = vec4(normalized, 0.0, 1.0);\n"
    "}\n";

static const char *solid_fragment_shader_source =
    "precision mediump float;\n"
    "uniform vec4 color;\n"
    "void main() {\n"
    "    gl_FragColor = color;\n"
    "}\n";

static GLuint shader_program = 0;
static GLint attr_position = -1;
static GLint attr_texcoord = -1;
static GLint uniform_screen_size = -1;
static GLint uniform_surface_pos = -1;
static GLint uniform_surface_size = -1;
static GLint uniform_texture = -1;
static GLint uniform_src_rect = -1;

static GLuint solid_shader_program = 0;
static GLint solid_attr_position = -1;
static GLint solid_uniform_screen_size = -1;
static GLint solid_uniform_rect_pos = -1;
static GLint solid_uniform_rect_size = -1;
static GLint solid_uniform_color = -1;

static GLuint quad_vbo = 0;
static int current_screen_width = 0;
static int current_screen_height = 0;

static float quad_vertices[] = {
    0.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f,
};

static GLuint compile_shader(GLenum type, const char *source) {
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
    uniform_src_rect = glGetUniformLocation(shader_program, "src_rect");

    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* solid color shader */
    GLuint solid_vs = compile_shader(GL_VERTEX_SHADER, solid_vertex_shader_source);
    GLuint solid_fs = compile_shader(GL_FRAGMENT_SHADER, solid_fragment_shader_source);
    if (!solid_vs || !solid_fs) {
        fprintf(stderr, "owl: failed to compile solid shaders\n");
        return false;
    }

    solid_shader_program = glCreateProgram();
    glAttachShader(solid_shader_program, solid_vs);
    glAttachShader(solid_shader_program, solid_fs);
    glLinkProgram(solid_shader_program);
    glDeleteShader(solid_vs);
    glDeleteShader(solid_fs);

    glGetProgramiv(solid_shader_program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        glGetProgramInfoLog(solid_shader_program, sizeof(log), NULL, log);
        fprintf(stderr, "owl: solid shader link error: %s\n", log);
        return false;
    }

    solid_attr_position = glGetAttribLocation(solid_shader_program, "position");
    solid_uniform_screen_size = glGetUniformLocation(solid_shader_program, "screen_size");
    solid_uniform_rect_pos = glGetUniformLocation(solid_shader_program, "rect_pos");
    solid_uniform_rect_size = glGetUniformLocation(solid_shader_program, "rect_size");
    solid_uniform_color = glGetUniformLocation(solid_shader_program, "color");

    fprintf(stderr, "owl: shaders initialized\n");
    return true;
}

static uint32_t get_framebuffer_for_bo(owl_display *display, struct gbm_bo *bo) {
    uint32_t *fb_id_ptr = gbm_bo_get_user_data(bo);
    if (fb_id_ptr) {
        return *fb_id_ptr;
    }

    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;

    uint32_t *fb_id = malloc(sizeof(uint32_t));
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

static bool has_egl_extension(EGLDisplay dpy, const char *ext) {
	const char *exts = eglQueryString(dpy, EGL_EXTENSIONS);
	if (!exts) return false;
	size_t len = strlen(ext);
	const char *p = exts;
	while ((p = strstr(p, ext)) != NULL) {
		if ((p == exts || p[-1] == ' ') && (p[len] == '\0' || p[len] == ' '))
			return true;
		p += len;
	}
	return false;
}

static void init_dmabuf_formats(owl_display *display) {
	if (!eglQueryDmaBufFormatsEXT) return;

	EGLint count = 0;
	if (!eglQueryDmaBufFormatsEXT(display->egl_display, 0, NULL, &count) || count == 0)
		return;

	EGLint *formats = calloc(count, sizeof(EGLint));
	if (!formats) return;

	if (!eglQueryDmaBufFormatsEXT(display->egl_display, count, formats, &count)) {
		free(formats);
		return;
	}

	int added = 0;
	for (int i = 0; i < count && added < OWL_MAX_DRM_FORMATS; i++) {
		uint32_t fmt = formats[i];

		EGLint mod_count = 0;
		uint64_t *modifiers = NULL;

		if (eglQueryDmaBufModifiersEXT) {
			eglQueryDmaBufModifiersEXT(display->egl_display, fmt, 0, NULL, NULL, &mod_count);
			if (mod_count > 0) {
				modifiers = calloc(mod_count, sizeof(uint64_t));
				if (modifiers) {
					eglQueryDmaBufModifiersEXT(display->egl_display, fmt, mod_count,
						modifiers, NULL, &mod_count);
				}
			}
		}

		if (mod_count == 0) {
			modifiers = calloc(1, sizeof(uint64_t));
			if (modifiers) {
				modifiers[0] = DRM_FORMAT_MOD_INVALID;
				mod_count = 1;
			}
		}

		display->dmabuf_formats[added].format = fmt;
		display->dmabuf_formats[added].modifiers = modifiers;
		display->dmabuf_formats[added].modifier_count = mod_count;
		added++;
	}
	display->dmabuf_format_count = added;
	free(formats);
}

void owl_render_init(owl_display *display) {
	if (!eglMakeCurrent(display->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, display->egl_context)) {
		fprintf(stderr, "owl: failed to make EGL context current for init\n");
		return;
	}

	if (!init_shaders()) {
		fprintf(stderr, "owl: failed to initialize shaders\n");
	}

	eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
	glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
		eglGetProcAddress("glEGLImageTargetTexture2DOES");

	if (has_egl_extension(display->egl_display, "EGL_EXT_image_dma_buf_import") &&
	    eglCreateImageKHR && eglDestroyImageKHR && glEGLImageTargetTexture2DOES) {
		dmabuf_import_supported = true;
		display->dmabuf_import_supported = true;
		fprintf(stderr, "owl: EGL dmabuf import supported\n");
	}

	if (has_egl_extension(display->egl_display, "EGL_EXT_image_dma_buf_import_modifiers")) {
		eglQueryDmaBufFormatsEXT = (PFNEGLQUERYDMABUFFORMATSEXTPROC)
			eglGetProcAddress("eglQueryDmaBufFormatsEXT");
		eglQueryDmaBufModifiersEXT = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)
			eglGetProcAddress("eglQueryDmaBufModifiersEXT");
		if (eglQueryDmaBufFormatsEXT && eglQueryDmaBufModifiersEXT)
			dmabuf_modifiers_supported = true;
	}

	if (dmabuf_import_supported)
		init_dmabuf_formats(display);
}

void owl_render_cleanup(owl_display *display) {
	for (int i = 0; i < display->dmabuf_format_count; i++) {
		free(display->dmabuf_formats[i].modifiers);
	}
	display->dmabuf_format_count = 0;

	if (quad_vbo) {
		glDeleteBuffers(1, &quad_vbo);
		quad_vbo = 0;
	}

	if (shader_program) {
		glDeleteProgram(shader_program);
		shader_program = 0;
	}

	if (solid_shader_program) {
		glDeleteProgram(solid_shader_program);
		solid_shader_program = 0;
	}
}

bool owl_dmabuf_import(owl_display *display, owl_dmabuf_buffer *buffer) {
	if (!dmabuf_import_supported || !buffer || buffer->plane_count < 1)
		return false;

	if (!eglMakeCurrent(display->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, display->egl_context))
		return false;

	bool has_modifier = buffer->modifier != DRM_FORMAT_MOD_INVALID;

	int attr_idx = 0;
	EGLint attribs[64];
	attribs[attr_idx++] = EGL_WIDTH;
	attribs[attr_idx++] = buffer->width;
	attribs[attr_idx++] = EGL_HEIGHT;
	attribs[attr_idx++] = buffer->height;
	attribs[attr_idx++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[attr_idx++] = buffer->format;

	struct {
		EGLint fd, offset, pitch, mod_lo, mod_hi;
	} plane_attribs[4] = {
		{ EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE0_PITCH_EXT,
		  EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT },
		{ EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
		  EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT },
		{ EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT,
		  EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT },
		{ EGL_DMA_BUF_PLANE3_FD_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT,
		  EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT },
	};

	for (int i = 0; i < buffer->plane_count; i++) {
		attribs[attr_idx++] = plane_attribs[i].fd;
		attribs[attr_idx++] = buffer->fds[i];
		attribs[attr_idx++] = plane_attribs[i].offset;
		attribs[attr_idx++] = buffer->offsets[i];
		attribs[attr_idx++] = plane_attribs[i].pitch;
		attribs[attr_idx++] = buffer->strides[i];

		if (has_modifier && dmabuf_modifiers_supported) {
			attribs[attr_idx++] = plane_attribs[i].mod_lo;
			attribs[attr_idx++] = buffer->modifier & 0xFFFFFFFF;
			attribs[attr_idx++] = plane_attribs[i].mod_hi;
			attribs[attr_idx++] = buffer->modifier >> 32;
		}
	}
	attribs[attr_idx++] = EGL_NONE;

	EGLImageKHR image = eglCreateImageKHR(display->egl_display, EGL_NO_CONTEXT,
		EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
	if (image == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "owl: failed to create EGLImage from dmabuf\n");
		return false;
	}

	if (buffer->texture_id == 0)
		glGenTextures(1, &buffer->texture_id);

	glBindTexture(GL_TEXTURE_2D, buffer->texture_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (buffer->egl_image)
		eglDestroyImageKHR(display->egl_display, buffer->egl_image);
	buffer->egl_image = image;

	return true;
}

void owl_dmabuf_buffer_destroy(owl_dmabuf_buffer *buffer) {
	if (!buffer) return;

	if (buffer->egl_image && buffer->display)
		eglDestroyImageKHR(buffer->display->egl_display, buffer->egl_image);

	if (buffer->texture_id)
		glDeleteTextures(1, &buffer->texture_id);

	for (int i = 0; i < buffer->plane_count; i++) {
		if (buffer->fds[i] >= 0)
			close(buffer->fds[i]);
	}

	free(buffer);
}

void owl_render_rect(int x, int y, int w, int h, float r, float g, float b, float a) {
    glUseProgram(solid_shader_program);

    glUniform2f(solid_uniform_screen_size, (float)current_screen_width, (float)current_screen_height);
    glUniform2f(solid_uniform_rect_pos, (float)x, (float)y);
    glUniform2f(solid_uniform_rect_size, (float)w, (float)h);
    glUniform4f(solid_uniform_color, r, g, b, a);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(solid_attr_position);
    glVertexAttribPointer(solid_attr_position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(solid_attr_position);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

uint32_t owl_render_upload_texture(owl_display *display, owl_surface *surface) {
	if (!surface || !surface->current.buffer)
		return 0;

	if (surface->current.buffer_type == OWL_BUFFER_DMABUF) {
		owl_dmabuf_buffer *buffer = surface->current.buffer;
		if (!buffer || buffer->failed)
			return 0;

		if (!buffer->egl_image) {
			if (!owl_dmabuf_import(display, buffer)) {
				buffer->failed = true;
				return 0;
			}
		}

		surface->texture_id = buffer->texture_id;
		surface->texture_width = buffer->width;
		surface->texture_height = buffer->height;
		wl_buffer_send_release(buffer->resource);
		return surface->texture_id;
	}

	owl_shm_buffer *buffer = surface->current.buffer;
	owl_shm_pool *pool = buffer->pool;

	if (!pool || !pool->data)
		return 0;

	if (!eglMakeCurrent(display->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, display->egl_context))
		return 0;

	if (surface->texture_id == 0)
		glGenTextures(1, &surface->texture_id);

	glBindTexture(GL_TEXTURE_2D, surface->texture_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	void *pixels = (char *)pool->data + buffer->offset;

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

static bool should_block_out(owl_render_target target, owl_block_out_from block) {
	if (block == OWL_BLOCK_OUT_NONE) return false;
	if (block == OWL_BLOCK_OUT_SCREENCAST && target == OWL_RENDER_TARGET_SCREENCAST) return true;
	if (block == OWL_BLOCK_OUT_SCREEN_CAPTURE && target != OWL_RENDER_TARGET_OUTPUT) return true;
	return false;
}

void owl_render_surface(owl_display *display, owl_surface *surface, int x, int y) {
	(void)display;

	if (!surface || surface->texture_id == 0)
		return;

	glUseProgram(shader_program);

	float render_width = surface->texture_width;
	float render_height = surface->texture_height;
	float src_x = 0.0f, src_y = 0.0f;
	float src_w = 1.0f, src_h = 1.0f;

	owl_viewport_state *vp = &surface->current.viewport;

	if (vp->has_src) {
		src_x = vp->src_x / surface->texture_width;
		src_y = vp->src_y / surface->texture_height;
		src_w = vp->src_width / surface->texture_width;
		src_h = vp->src_height / surface->texture_height;
		if (!vp->has_dst) {
			render_width = vp->src_width;
			render_height = vp->src_height;
		}
	}

	if (vp->has_dst) {
		render_width = vp->dst_width;
		render_height = vp->dst_height;
	}

	glUniform2f(uniform_surface_pos, (float)x, (float)y);
	glUniform2f(uniform_surface_size, render_width, render_height);
	glUniform4f(uniform_src_rect, src_x, src_y, src_w, src_h);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, surface->texture_id);
	glUniform1i(uniform_texture, 0);

	glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
	glEnableVertexAttribArray(attr_position);
	glEnableVertexAttribArray(attr_texcoord);
	glVertexAttribPointer(attr_position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
	glVertexAttribPointer(attr_texcoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

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
static void owl_layer_surface_get_position(owl_layer_surface *ls, owl_output *output, int *x, int *y) {
    int out_w = output->width;
    int out_h = output->height;
    int surf_w = ls->surface->texture_width;
    int surf_h = ls->surface->texture_height;

    uint32_t both_horiz = OWL_ANCHOR_LEFT | OWL_ANCHOR_RIGHT;
    uint32_t both_vert = OWL_ANCHOR_TOP | OWL_ANCHOR_BOTTOM;

    if ((ls->anchor & both_horiz) == both_horiz) {
        *x = (out_w - surf_w) / 2;
    } else if (ls->anchor & OWL_ANCHOR_LEFT) {
        *x = ls->margin_left;
    } else if (ls->anchor & OWL_ANCHOR_RIGHT) {
        *x = out_w - surf_w - ls->margin_right;
    } else {
        *x = (out_w - surf_w) / 2;
    }

    if ((ls->anchor & both_vert) == both_vert) {
        *y = (out_h - surf_h) / 2;
    } else if (ls->anchor & OWL_ANCHOR_TOP) {
        *y = ls->margin_top;
    } else if (ls->anchor & OWL_ANCHOR_BOTTOM) {
        *y = out_h - surf_h - ls->margin_bottom;
    } else {
        *y = (out_h - surf_h) / 2;
    }
}

static void render_layer_surfaces(owl_display *display, owl_output *output, owl_layer layer) {
    for (int i = 0; i < display->layer_surface_count; i++) {
        owl_layer_surface *ls = display->layer_surfaces[i];
        if (ls->layer != layer || !ls->mapped || !ls->surface || !ls->surface->has_content) {
            continue;
        }
        int x, y;
        owl_layer_surface_get_position(ls, output, &x, &y);
        owl_render_surface(display, ls->surface, x, y);
    }
}

void owl_render_frame(owl_display *display, owl_output *output) {
    if (!display || !output) {
        return;
    }

    if (output->page_flip_pending) {
        return;
    }

    display->current_render_target = OWL_RENDER_TARGET_OUTPUT;

    if (!eglMakeCurrent(display->egl_display, output->egl_surface,
                        output->egl_surface, display->egl_context)) {
        fprintf(stderr, "owl: failed to make EGL context current\n");
        return;
    }

    glViewport(0, 0, output->width, output->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    current_screen_width = output->width;
    current_screen_height = output->height;

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shader_program);
    glUniform2f(uniform_screen_size, (float)output->width, (float)output->height);

    render_layer_surfaces(display, output, OWL_LAYER_BACKGROUND);
    render_layer_surfaces(display, output, OWL_LAYER_BOTTOM);

    owl_window *window;
    wl_list_for_each_reverse(window, &display->windows, link) {
        if (window->mapped && window->surface && window->surface->has_content) {
            if (display->render_callback) {
                display->render_callback(display, window, display->render_callback_data);
            }
            if (should_block_out(display->current_render_target, window->block_out_from)) {
                owl_render_rect(window->x, window->y, window->width, window->height, 0.0f, 0.0f, 0.0f, 1.0f);
            } else {
                owl_render_surface(display, window->surface, window->x, window->y);
            }
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

    bool has_pending_screencopy = false;
    for (int i = 0; i < display->screencopy_frame_count; i++) {
        owl_screencopy_frame *frame = display->screencopy_frames[i];
        if (frame && frame->state == OWL_SCREENCOPY_FRAME_COPYING && frame->output == output) {
            has_pending_screencopy = true;
            break;
        }
    }

    if (has_pending_screencopy) {
        display->current_render_target = OWL_RENDER_TARGET_SCREENSHOT;

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(shader_program);

        wl_list_for_each_reverse(window, &display->windows, link) {
            if (window->mapped && window->surface && window->surface->has_content) {
                if (should_block_out(display->current_render_target, window->block_out_from)) {
                    owl_render_rect(window->x, window->y, window->width, window->height, 0.0f, 0.0f, 0.0f, 1.0f);
                } else {
                    owl_render_surface(display, window->surface, window->x, window->y);
                }
            }
        }

        glDisable(GL_BLEND);

        for (int i = 0; i < display->screencopy_frame_count; i++) {
            owl_screencopy_frame *frame = display->screencopy_frames[i];
            if (frame && frame->state == OWL_SCREENCOPY_FRAME_COPYING && frame->output == output) {
                owl_screencopy_do_copy(frame);
            }
        }

        display->current_render_target = OWL_RENDER_TARGET_OUTPUT;

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(shader_program);

        wl_list_for_each_reverse(window, &display->windows, link) {
            if (window->mapped && window->surface && window->surface->has_content) {
                owl_render_surface(display, window->surface, window->x, window->y);
            }
        }

        glDisable(GL_BLEND);
    }

    if (!eglSwapBuffers(display->egl_display, output->egl_surface)) {
        fprintf(stderr, "owl: failed to swap buffers\n");
        return;
    }

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(output->gbm_surface);
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

void owl_set_render_callback(owl_display *display, owl_render_callback callback, void *data) {
    if (!display) return;
    display->render_callback = callback;
    display->render_callback_data = data;
}

void owl_screencopy_do_copy(owl_screencopy_frame *frame) {
    if (!frame || !frame->buffer || !frame->output || !frame->display) {
        if (frame && frame->resource) {
            zwlr_screencopy_frame_v1_send_failed(frame->resource);
        }
        return;
    }

    owl_display *display = frame->display;
    owl_output *output = frame->output;
    owl_shm_buffer *buffer = frame->buffer;

    if (!buffer->pool || !buffer->pool->data) {
        zwlr_screencopy_frame_v1_send_failed(frame->resource);
        return;
    }

    uint32_t expected_stride = frame->width * 4;
    if ((uint32_t)buffer->stride != expected_stride) {
        zwlr_screencopy_frame_v1_send_failed(frame->resource);
        return;
    }

    size_t required_size = (size_t)buffer->stride * buffer->height;
    if (buffer->offset + required_size > (size_t)buffer->pool->size) {
        zwlr_screencopy_frame_v1_send_failed(frame->resource);
        return;
    }

    if (!eglMakeCurrent(display->egl_display, output->egl_surface,
                        output->egl_surface, display->egl_context)) {
        zwlr_screencopy_frame_v1_send_failed(frame->resource);
        return;
    }

    void *dst = (char *)buffer->pool->data + buffer->offset;
    int gl_y = output->height - frame->y - frame->height;

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
    glReadPixels(frame->x, gl_y, frame->width, frame->height,
                 GL_BGRA_EXT, GL_UNSIGNED_BYTE, dst);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "owl: glReadPixels failed: 0x%x\n", err);
        zwlr_screencopy_frame_v1_send_failed(frame->resource);
        return;
    }

    // Mark as done so it's not processed again
    frame->state = OWL_SCREENCOPY_FRAME_PENDING;
    frame->buffer = NULL;

    zwlr_screencopy_frame_v1_send_flags(frame->resource, ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t tv_sec_hi = (uint32_t)(ts.tv_sec >> 32);
    uint32_t tv_sec_lo = (uint32_t)(ts.tv_sec & 0xFFFFFFFF);
    uint32_t tv_nsec = (uint32_t)ts.tv_nsec;

    zwlr_screencopy_frame_v1_send_ready(frame->resource, tv_sec_hi, tv_sec_lo, tv_nsec);
}
