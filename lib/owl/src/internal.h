#ifndef OWL_INTERNAL_H
#define OWL_INTERNAL_H

#include <owl/owl.h>
#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>
#include <xf86drmMode.h>
#include <stdbool.h>
#include <stdint.h>

#define OWL_MAX_OUTPUTS 8
#define OWL_MAX_WINDOWS 256
#define OWL_MAX_CALLBACKS 16

struct Owl_Output {
    struct Owl_Display* display;
    int pos_x;
    int pos_y;
    int width;
    int height;
    char* name;
    uint32_t drm_connector_id;
    uint32_t drm_crtc_id;
    drmModeModeInfo drm_mode;
    struct gbm_surface* gbm_surface;
    void* egl_surface;
    uint32_t framebuffer_id;
    struct gbm_bo* current_bo;
    struct gbm_bo* next_bo;
    bool page_flip_pending;
    struct wl_global* wl_output_global;
};

typedef struct Owl_Shm_Pool {
    struct Owl_Display* display;
    struct wl_resource* resource;
    int fd;
    void* data;
    int32_t size;
    int ref_count;
} Owl_Shm_Pool;

typedef struct Owl_Shm_Buffer {
    struct wl_resource* resource;
    Owl_Shm_Pool* pool;
    int32_t offset;
    int32_t width;
    int32_t height;
    int32_t stride;
    uint32_t format;
    bool busy;
} Owl_Shm_Buffer;

typedef struct Owl_Surface_State {
    Owl_Shm_Buffer* buffer;
    int32_t buffer_x;
    int32_t buffer_y;
    bool buffer_attached;
    struct wl_list frame_callbacks;
    int32_t damage_x;
    int32_t damage_y;
    int32_t damage_width;
    int32_t damage_height;
    bool has_damage;
} Owl_Surface_State;

typedef struct Owl_Surface {
    struct Owl_Display* display;
    struct wl_resource* resource;
    Owl_Surface_State pending;
    Owl_Surface_State current;
    uint32_t texture_id;
    int32_t texture_width;
    int32_t texture_height;
    bool has_content;
    struct wl_list link;
} Owl_Surface;

typedef struct Owl_Frame_Callback {
    struct wl_resource* resource;
    struct wl_list link;
} Owl_Frame_Callback;

struct Owl_Window {
    struct Owl_Display* display;
    Owl_Surface* surface;
    struct wl_resource* xdg_surface_resource;
    struct wl_resource* xdg_toplevel_resource;
    int pos_x;
    int pos_y;
    int width;
    int height;
    char* title;
    char* app_id;
    bool fullscreen;
    bool focused;
    bool mapped;
    uint32_t pending_serial;
    bool pending_configure;
    struct wl_list link;
};

struct Owl_Input {
    uint32_t keycode;
    uint32_t keysym;
    uint32_t modifiers;
    uint32_t button;
    int pointer_x;
    int pointer_y;
};

typedef struct {
    Owl_Window_Callback callback;
    void* data;
} Window_Callback_Entry;

typedef struct {
    Owl_Input_Callback callback;
    void* data;
} Input_Callback_Entry;

typedef struct {
    Owl_Output_Callback callback;
    void* data;
} Output_Callback_Entry;

struct Owl_Display {
    struct wl_display* wayland_display;
    struct wl_event_loop* event_loop;
    const char* socket_name;
    bool running;

    int drm_fd;
    struct gbm_device* gbm_device;
    void* egl_display;
    void* egl_context;
    void* egl_config;

    struct libinput* libinput;
    struct udev* udev;
    struct xkb_context* xkb_context;
    struct xkb_keymap* xkb_keymap;
    struct xkb_state* xkb_state;
    uint32_t modifier_state;

    struct Owl_Output* outputs[OWL_MAX_OUTPUTS];
    int output_count;

    struct wl_list windows;
    int window_count;

    struct wl_list surfaces;
    int surface_count;
    struct wl_global* compositor_global;
    struct wl_global* shm_global;
    struct wl_global* subcompositor_global;
    struct wl_global* data_device_manager_global;

    Window_Callback_Entry window_callbacks[12][OWL_MAX_CALLBACKS];
    int window_callback_count[12];

    Input_Callback_Entry input_callbacks[5][OWL_MAX_CALLBACKS];
    int input_callback_count[5];

    Output_Callback_Entry output_callbacks[3][OWL_MAX_CALLBACKS];
    int output_callback_count[3];

    struct wl_event_source* drm_event_source;
    struct wl_event_source* libinput_event_source;

    struct wl_global* seat_global;
    struct wl_list keyboards;
    struct wl_list pointers;
    Owl_Surface* keyboard_focus;
    Owl_Surface* pointer_focus;
    double pointer_x;
    double pointer_y;
    int keymap_fd;
    uint32_t keymap_size;

    Owl_Surface* cursor_surface;
    int32_t cursor_hotspot_x;
    int32_t cursor_hotspot_y;
};

void owl_output_init(Owl_Display* display);
void owl_output_cleanup(Owl_Display* display);
void owl_output_render_frame(Owl_Output* output);

void owl_input_init(Owl_Display* display);
void owl_input_cleanup(Owl_Display* display);
void owl_input_process_events(Owl_Display* display);

void owl_render_init(Owl_Display* display);
void owl_render_cleanup(Owl_Display* display);
void owl_render_frame(Owl_Display* display, Owl_Output* output);

void owl_invoke_window_callback(Owl_Display* display, Owl_Window_Event type, Owl_Window* window);
void owl_invoke_input_callback(Owl_Display* display, Owl_Input_Event type, Owl_Input* input);
void owl_invoke_output_callback(Owl_Display* display, Owl_Output_Event type, Owl_Output* output);

void owl_surface_init(Owl_Display* display);
void owl_surface_cleanup(Owl_Display* display);
Owl_Surface* owl_surface_from_resource(struct wl_resource* resource);
void owl_surface_send_frame_done(Owl_Display* display, uint32_t time);

void owl_xdg_shell_init(Owl_Display* display);
void owl_xdg_shell_cleanup(Owl_Display* display);
void owl_xdg_toplevel_send_configure(Owl_Window* window, int width, int height);
void owl_xdg_toplevel_send_close(Owl_Window* window);
void owl_window_map(Owl_Window* window);

void owl_seat_init(Owl_Display* display);
void owl_seat_cleanup(Owl_Display* display);
void owl_seat_set_keyboard_focus(Owl_Display* display, Owl_Surface* surface);
void owl_seat_set_pointer_focus(Owl_Display* display, Owl_Surface* surface, double x, double y);
void owl_seat_send_key(Owl_Display* display, uint32_t key, uint32_t state);
void owl_seat_send_modifiers(Owl_Display* display);
void owl_seat_send_pointer_motion(Owl_Display* display, double x, double y);
void owl_seat_send_pointer_button(Owl_Display* display, uint32_t button, uint32_t state);

uint32_t owl_render_upload_texture(Owl_Display* display, Owl_Surface* surface);
void owl_render_surface(Owl_Display* display, Owl_Surface* surface, int x, int y);

#endif
