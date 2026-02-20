#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <libudev.h>
#include <libinput.h>
#include <wayland-server-protocol.h>

static FILE* input_log = NULL;
static void input_debug(const char* fmt, ...) {
    if (!input_log) input_log = fopen("/tmp/owl_input.log", "w");
    if (input_log) {
        va_list args;
        va_start(args, fmt);
        vfprintf(input_log, fmt, args);
        va_end(args);
        fflush(input_log);
    }
}

typedef struct Owl_Keyboard {
    struct wl_resource* resource;
    struct wl_list link;
} Owl_Keyboard;

typedef struct Owl_Pointer {
    struct wl_resource* resource;
    struct wl_list link;
    Owl_Display* display;
} Owl_Pointer;

static uint32_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int open_restricted(const char* path, int flags, void* user_data) {
    (void)user_data;
    int fd = open(path, flags);
    if (fd < 0) {
        fprintf(stderr, "owl: failed to open %s: %s\n", path, strerror(errno));
        return -errno;
    }
    fprintf(stderr, "owl: opened input device %s\n", path);
    return fd;
}

static void close_restricted(int fd, void* user_data) {
    (void)user_data;
    close(fd);
}

static const struct libinput_interface libinput_interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static void update_modifier_state(Owl_Display* display) {
    if (!display->xkb_state) {
        return;
    }

    display->modifier_state = 0;

    if (xkb_state_mod_name_is_active(display->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE)) {
        display->modifier_state |= OWL_MOD_SHIFT;
    }
    if (xkb_state_mod_name_is_active(display->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE)) {
        display->modifier_state |= OWL_MOD_CTRL;
    }
    if (xkb_state_mod_name_is_active(display->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE)) {
        display->modifier_state |= OWL_MOD_ALT;
    }
    if (xkb_state_mod_name_is_active(display->xkb_state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE)) {
        display->modifier_state |= OWL_MOD_SUPER;
    }
}

static void handle_keyboard_key(Owl_Display* display, struct libinput_event_keyboard* event) {
    uint32_t keycode = libinput_event_keyboard_get_key(event);
    enum libinput_key_state state = libinput_event_keyboard_get_key_state(event);

    if (display->xkb_state) {
        xkb_state_update_key(display->xkb_state, keycode + 8,
                            state == LIBINPUT_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
        update_modifier_state(display);
    }

    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    if (display->xkb_state) {
        keysym = xkb_state_key_get_one_sym(display->xkb_state, keycode + 8);
    }

    struct Owl_Input input = {
        .keycode = keycode,
        .keysym = keysym,
        .modifiers = display->modifier_state,
        .button = 0,
        .pointer_x = (int)display->pointer_x,
        .pointer_y = (int)display->pointer_y,
    };

    Owl_Input_Event event_type = state == LIBINPUT_KEY_STATE_PRESSED
        ? OWL_INPUT_KEY_PRESS : OWL_INPUT_KEY_RELEASE;

    owl_invoke_input_callback(display, event_type, &input);

    uint32_t wl_state = state == LIBINPUT_KEY_STATE_PRESSED
        ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;
    owl_seat_send_key(display, keycode, wl_state);
    owl_seat_send_modifiers(display);
}

static Owl_Window* find_window_at(Owl_Display* display, int x, int y) {
    Owl_Window* window;
    wl_list_for_each(window, &display->windows, link) {
        if (!window->mapped) {
            continue;
        }
        if (x >= window->pos_x && x < window->pos_x + window->width &&
            y >= window->pos_y && y < window->pos_y + window->height) {
            return window;
        }
    }
    return NULL;
}

static void update_pointer_focus(Owl_Display* display) {
    int px = (int)display->pointer_x;
    int py = (int)display->pointer_y;

    Owl_Window* window = find_window_at(display, px, py);
    Owl_Surface* surface = window ? window->surface : NULL;

    if (surface != display->pointer_focus) {
        double local_x = px - (window ? window->pos_x : 0);
        double local_y = py - (window ? window->pos_y : 0);
        owl_seat_set_pointer_focus(display, surface, local_x, local_y);
    }
}

static void handle_pointer_motion(Owl_Display* display, struct libinput_event_pointer* event) {
    double dx = libinput_event_pointer_get_dx(event);
    double dy = libinput_event_pointer_get_dy(event);

    display->pointer_x += dx;
    display->pointer_y += dy;

    if (display->output_count > 0) {
        Owl_Output* output = display->outputs[0];
        if (display->pointer_x < 0) display->pointer_x = 0;
        if (display->pointer_y < 0) display->pointer_y = 0;
        if (display->pointer_x >= output->width) display->pointer_x = output->width - 1;
        if (display->pointer_y >= output->height) display->pointer_y = output->height - 1;
    }

    update_pointer_focus(display);

    struct Owl_Input input = {
        .keycode = 0,
        .keysym = 0,
        .modifiers = display->modifier_state,
        .button = 0,
        .pointer_x = (int)display->pointer_x,
        .pointer_y = (int)display->pointer_y,
    };

    owl_invoke_input_callback(display, OWL_INPUT_POINTER_MOTION, &input);

    Owl_Window* focused_window = find_window_at(display, (int)display->pointer_x, (int)display->pointer_y);
    if (focused_window) {
        double local_x = display->pointer_x - focused_window->pos_x;
        double local_y = display->pointer_y - focused_window->pos_y;
        owl_seat_send_pointer_motion(display, local_x, local_y);
    }
}

static void handle_pointer_button(Owl_Display* display, struct libinput_event_pointer* event) {
    uint32_t button = libinput_event_pointer_get_button(event);
    enum libinput_button_state state = libinput_event_pointer_get_button_state(event);

    struct Owl_Input input = {
        .keycode = 0,
        .keysym = 0,
        .modifiers = display->modifier_state,
        .button = button,
        .pointer_x = (int)display->pointer_x,
        .pointer_y = (int)display->pointer_y,
    };

    Owl_Input_Event event_type = state == LIBINPUT_BUTTON_STATE_PRESSED
        ? OWL_INPUT_BUTTON_PRESS : OWL_INPUT_BUTTON_RELEASE;

    owl_invoke_input_callback(display, event_type, &input);

    uint32_t wl_state = state == LIBINPUT_BUTTON_STATE_PRESSED
        ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED;
    owl_seat_send_pointer_button(display, button, wl_state);
}

static int handle_libinput_event(int fd, uint32_t mask, void* data) {
    (void)fd;
    (void)mask;
    Owl_Display* display = data;

    if (libinput_dispatch(display->libinput) != 0) {
        fprintf(stderr, "owl: failed to dispatch libinput\n");
        return 0;
    }

    struct libinput_event* event;
    while ((event = libinput_get_event(display->libinput)) != NULL) {
        enum libinput_event_type type = libinput_event_get_type(event);

        switch (type) {
            case LIBINPUT_EVENT_KEYBOARD_KEY:
                handle_keyboard_key(display, libinput_event_get_keyboard_event(event));
                break;
            case LIBINPUT_EVENT_POINTER_MOTION:
                handle_pointer_motion(display, libinput_event_get_pointer_event(event));
                break;
            case LIBINPUT_EVENT_POINTER_BUTTON:
                handle_pointer_button(display, libinput_event_get_pointer_event(event));
                break;
            default:
                break;
        }

        libinput_event_destroy(event);
    }

    return 0;
}

static int create_keymap_fd(Owl_Display* display) {
    if (!display->xkb_keymap) {
        return -1;
    }

    char* keymap_string = xkb_keymap_get_as_string(display->xkb_keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!keymap_string) {
        return -1;
    }

    display->keymap_size = strlen(keymap_string) + 1;

    char name[] = "/tmp/owl-keymap-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) {
        free(keymap_string);
        return -1;
    }

    unlink(name);

    if (ftruncate(fd, display->keymap_size) < 0) {
        close(fd);
        free(keymap_string);
        return -1;
    }

    void* ptr = mmap(NULL, display->keymap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        close(fd);
        free(keymap_string);
        return -1;
    }

    memcpy(ptr, keymap_string, display->keymap_size);
    munmap(ptr, display->keymap_size);
    free(keymap_string);

    return fd;
}

void owl_input_init(Owl_Display* display) {
    display->udev = udev_new();
    if (!display->udev) {
        fprintf(stderr, "owl: failed to create udev context\n");
        return;
    }

    display->libinput = libinput_udev_create_context(
        &libinput_interface, display, display->udev);

    if (!display->libinput) {
        fprintf(stderr, "owl: failed to create libinput context\n");
        udev_unref(display->udev);
        display->udev = NULL;
        return;
    }

    if (libinput_udev_assign_seat(display->libinput, "seat0") != 0) {
        fprintf(stderr, "owl: failed to assign seat\n");
        libinput_unref(display->libinput);
        display->libinput = NULL;
        udev_unref(display->udev);
        display->udev = NULL;
        return;
    }

    display->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (display->xkb_context) {
        display->xkb_keymap = xkb_keymap_new_from_names(
            display->xkb_context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

        if (display->xkb_keymap) {
            display->xkb_state = xkb_state_new(display->xkb_keymap);
        }
    }

    display->keymap_fd = create_keymap_fd(display);

    int libinput_fd = libinput_get_fd(display->libinput);
    display->libinput_event_source = wl_event_loop_add_fd(
        display->event_loop, libinput_fd,
        WL_EVENT_READABLE, handle_libinput_event, display);

    libinput_dispatch(display->libinput);
    struct libinput_event* event;
    while ((event = libinput_get_event(display->libinput)) != NULL) {
        enum libinput_event_type type = libinput_event_get_type(event);
        if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
            struct libinput_device* device = libinput_event_get_device(event);
            fprintf(stderr, "owl: input device added: %s\n", libinput_device_get_name(device));
        }
        libinput_event_destroy(event);
    }

    display->pointer_x = 0;
    display->pointer_y = 0;

    fprintf(stderr, "owl: input initialized\n");
}

void owl_input_cleanup(Owl_Display* display) {
    if (display->libinput_event_source) {
        wl_event_source_remove(display->libinput_event_source);
        display->libinput_event_source = NULL;
    }

    if (display->keymap_fd >= 0) {
        close(display->keymap_fd);
        display->keymap_fd = -1;
    }

    if (display->xkb_state) {
        xkb_state_unref(display->xkb_state);
        display->xkb_state = NULL;
    }

    if (display->xkb_keymap) {
        xkb_keymap_unref(display->xkb_keymap);
        display->xkb_keymap = NULL;
    }

    if (display->xkb_context) {
        xkb_context_unref(display->xkb_context);
        display->xkb_context = NULL;
    }

    if (display->libinput) {
        libinput_unref(display->libinput);
        display->libinput = NULL;
    }

    if (display->udev) {
        udev_unref(display->udev);
        display->udev = NULL;
    }
}

static void keyboard_release(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface keyboard_interface = {
    .release = keyboard_release,
};

static void keyboard_destroy_handler(struct wl_resource* resource) {
    Owl_Keyboard* keyboard = wl_resource_get_user_data(resource);
    if (keyboard) {
        wl_list_remove(&keyboard->link);
        free(keyboard);
    }
}

static void pointer_set_cursor(struct wl_client* client, struct wl_resource* resource,
                               uint32_t serial, struct wl_resource* surface_resource,
                               int32_t hotspot_x, int32_t hotspot_y) {
    (void)client;
    (void)serial;

    input_debug("pointer_set_cursor: surface_resource=%p hotspot=%d,%d\n",
                (void*)surface_resource, hotspot_x, hotspot_y);

    Owl_Pointer* pointer = wl_resource_get_user_data(resource);
    if (!pointer || !pointer->display) {
        input_debug("  no pointer or display\n");
        return;
    }

    Owl_Display* display = pointer->display;

    if (!surface_resource) {
        input_debug("  hiding cursor\n");
        display->cursor_surface = NULL;
        return;
    }

    Owl_Surface* cursor_surface = owl_surface_from_resource(surface_resource);
    if (!cursor_surface) {
        input_debug("  cursor surface not found\n");
        return;
    }

    input_debug("  cursor surface=%p has_content=%d\n",
                (void*)cursor_surface, cursor_surface->has_content);

    display->cursor_surface = cursor_surface;
    display->cursor_hotspot_x = hotspot_x;
    display->cursor_hotspot_y = hotspot_y;
}

static void pointer_release(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_pointer_interface pointer_interface = {
    .set_cursor = pointer_set_cursor,
    .release = pointer_release,
};

static void pointer_destroy_handler(struct wl_resource* resource) {
    Owl_Pointer* pointer = wl_resource_get_user_data(resource);
    if (pointer) {
        wl_list_remove(&pointer->link);
        free(pointer);
    }
}

static void seat_get_pointer(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    Owl_Display* display = wl_resource_get_user_data(resource);
    input_debug("seat_get_pointer: client=%p id=%u\n", (void*)client, id);

    Owl_Pointer* pointer = calloc(1, sizeof(Owl_Pointer));
    if (!pointer) {
        wl_resource_post_no_memory(resource);
        return;
    }

    uint32_t version = wl_resource_get_version(resource);
    pointer->resource = wl_resource_create(client, &wl_pointer_interface, version, id);
    if (!pointer->resource) {
        free(pointer);
        wl_resource_post_no_memory(resource);
        return;
    }

    pointer->display = display;
    wl_resource_set_implementation(pointer->resource, &pointer_interface, pointer, pointer_destroy_handler);
    wl_list_insert(&display->pointers, &pointer->link);
}

static void seat_get_keyboard(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    Owl_Display* display = wl_resource_get_user_data(resource);
    input_debug("seat_get_keyboard: client=%p id=%u\n", (void*)client, id);

    Owl_Keyboard* keyboard = calloc(1, sizeof(Owl_Keyboard));
    if (!keyboard) {
        wl_resource_post_no_memory(resource);
        return;
    }

    uint32_t version = wl_resource_get_version(resource);
    input_debug("  version=%u\n", version);
    keyboard->resource = wl_resource_create(client, &wl_keyboard_interface, version, id);
    if (!keyboard->resource) {
        free(keyboard);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(keyboard->resource, &keyboard_interface, keyboard, keyboard_destroy_handler);
    wl_list_insert(&display->keyboards, &keyboard->link);
    input_debug("  keyboard resource=%p\n", (void*)keyboard->resource);

    if (display->keymap_fd >= 0) {
        input_debug("  sending keymap fd=%d size=%u\n", display->keymap_fd, display->keymap_size);
        wl_keyboard_send_keymap(keyboard->resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                display->keymap_fd, display->keymap_size);
    }

    if (version >= 4) {
        input_debug("  sending repeat_info\n");
        wl_keyboard_send_repeat_info(keyboard->resource, 25, 600);
    }
    input_debug("seat_get_keyboard done\n");
}

static void seat_get_touch(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    (void)client;
    (void)resource;
    (void)id;
}

static void seat_release(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_seat_interface seat_interface = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = seat_release,
};

static void seat_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    Owl_Display* display = data;

    uint32_t bound_version = version < 7 ? version : 7;
    struct wl_resource* resource = wl_resource_create(client, &wl_seat_interface, bound_version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &seat_interface, display, NULL);

    wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);

    if (bound_version >= 2) {
        wl_seat_send_name(resource, "seat0");
    }
}

void owl_seat_init(Owl_Display* display) {
    wl_list_init(&display->keyboards);
    wl_list_init(&display->pointers);

    display->seat_global = wl_global_create(display->wayland_display,
        &wl_seat_interface, 7, display, seat_bind);

    if (!display->seat_global) {
        fprintf(stderr, "owl: failed to create wl_seat global\n");
        return;
    }

    fprintf(stderr, "owl: seat initialized\n");
}

void owl_seat_cleanup(Owl_Display* display) {
    if (display->seat_global) {
        wl_global_destroy(display->seat_global);
        display->seat_global = NULL;
    }
}

void owl_seat_set_keyboard_focus(Owl_Display* display, Owl_Surface* surface) {
    input_debug("owl_seat_set_keyboard_focus: surface=%p current_focus=%p\n",
                (void*)surface, (void*)display->keyboard_focus);

    if (display->keyboard_focus == surface) {
        input_debug("  same surface, returning\n");
        return;
    }

    uint32_t serial = wl_display_next_serial(display->wayland_display);
    input_debug("  serial=%u\n", serial);

    if (display->keyboard_focus) {
        struct wl_client* old_client = wl_resource_get_client(display->keyboard_focus->resource);
        input_debug("  sending leave to old client %p\n", (void*)old_client);
        Owl_Keyboard* keyboard;
        wl_list_for_each(keyboard, &display->keyboards, link) {
            if (wl_resource_get_client(keyboard->resource) == old_client) {
                input_debug("    sending leave to keyboard %p\n", (void*)keyboard->resource);
                wl_keyboard_send_leave(keyboard->resource, serial, display->keyboard_focus->resource);
            }
        }
    }

    display->keyboard_focus = surface;

    if (surface) {
        struct wl_client* new_client = wl_resource_get_client(surface->resource);
        input_debug("  sending enter to new client %p\n", (void*)new_client);
        struct wl_array keys;
        wl_array_init(&keys);

        int keyboard_count = 0;
        Owl_Keyboard* keyboard;
        wl_list_for_each(keyboard, &display->keyboards, link) {
            keyboard_count++;
        }
        input_debug("  total keyboards: %d\n", keyboard_count);

        wl_list_for_each(keyboard, &display->keyboards, link) {
            input_debug("    checking keyboard %p client=%p\n",
                        (void*)keyboard->resource,
                        (void*)wl_resource_get_client(keyboard->resource));
            if (wl_resource_get_client(keyboard->resource) == new_client) {
                input_debug("    sending enter to keyboard %p\n", (void*)keyboard->resource);
                wl_keyboard_send_enter(keyboard->resource, serial, surface->resource, &keys);
                input_debug("    enter sent\n");
            }
        }

        wl_array_release(&keys);
        input_debug("  sending modifiers\n");
        owl_seat_send_modifiers(display);
        input_debug("  modifiers sent\n");
    }
    input_debug("owl_seat_set_keyboard_focus done\n");
}

void owl_seat_set_pointer_focus(Owl_Display* display, Owl_Surface* surface, double x, double y) {
    if (display->pointer_focus == surface) {
        return;
    }

    uint32_t serial = wl_display_next_serial(display->wayland_display);

    if (display->pointer_focus) {
        struct wl_client* old_client = wl_resource_get_client(display->pointer_focus->resource);
        Owl_Pointer* pointer;
        wl_list_for_each(pointer, &display->pointers, link) {
            if (wl_resource_get_client(pointer->resource) == old_client) {
                wl_pointer_send_leave(pointer->resource, serial, display->pointer_focus->resource);
                wl_pointer_send_frame(pointer->resource);
            }
        }
    }

    display->pointer_focus = surface;

    if (surface) {
        struct wl_client* new_client = wl_resource_get_client(surface->resource);
        Owl_Pointer* pointer;
        wl_list_for_each(pointer, &display->pointers, link) {
            if (wl_resource_get_client(pointer->resource) == new_client) {
                wl_pointer_send_enter(pointer->resource, serial, surface->resource,
                                      wl_fixed_from_double(x), wl_fixed_from_double(y));
                wl_pointer_send_frame(pointer->resource);
            }
        }
    }
}

void owl_seat_send_key(Owl_Display* display, uint32_t key, uint32_t state) {
    if (!display->keyboard_focus) {
        return;
    }

    struct wl_client* client = wl_resource_get_client(display->keyboard_focus->resource);
    uint32_t serial = wl_display_next_serial(display->wayland_display);
    uint32_t time = get_time_ms();

    Owl_Keyboard* keyboard;
    wl_list_for_each(keyboard, &display->keyboards, link) {
        if (wl_resource_get_client(keyboard->resource) == client) {
            wl_keyboard_send_key(keyboard->resource, serial, time, key, state);
        }
    }
}

void owl_seat_send_modifiers(Owl_Display* display) {
    if (!display->keyboard_focus || !display->xkb_state) {
        return;
    }

    struct wl_client* client = wl_resource_get_client(display->keyboard_focus->resource);
    uint32_t serial = wl_display_next_serial(display->wayland_display);
    uint32_t depressed = xkb_state_serialize_mods(display->xkb_state, XKB_STATE_MODS_DEPRESSED);
    uint32_t latched = xkb_state_serialize_mods(display->xkb_state, XKB_STATE_MODS_LATCHED);
    uint32_t locked = xkb_state_serialize_mods(display->xkb_state, XKB_STATE_MODS_LOCKED);
    uint32_t group = xkb_state_serialize_layout(display->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);

    Owl_Keyboard* keyboard;
    wl_list_for_each(keyboard, &display->keyboards, link) {
        if (wl_resource_get_client(keyboard->resource) == client) {
            wl_keyboard_send_modifiers(keyboard->resource, serial, depressed, latched, locked, group);
        }
    }
}

void owl_seat_send_pointer_motion(Owl_Display* display, double x, double y) {
    if (!display->pointer_focus) {
        return;
    }

    struct wl_client* client = wl_resource_get_client(display->pointer_focus->resource);
    uint32_t time = get_time_ms();

    Owl_Pointer* pointer;
    wl_list_for_each(pointer, &display->pointers, link) {
        if (wl_resource_get_client(pointer->resource) == client) {
            wl_pointer_send_motion(pointer->resource, time,
                                   wl_fixed_from_double(x), wl_fixed_from_double(y));
            wl_pointer_send_frame(pointer->resource);
        }
    }
}

void owl_seat_send_pointer_button(Owl_Display* display, uint32_t button, uint32_t state) {
    if (!display->pointer_focus) {
        return;
    }

    struct wl_client* client = wl_resource_get_client(display->pointer_focus->resource);
    uint32_t serial = wl_display_next_serial(display->wayland_display);
    uint32_t time = get_time_ms();

    Owl_Pointer* pointer;
    wl_list_for_each(pointer, &display->pointers, link) {
        if (wl_resource_get_client(pointer->resource) == client) {
            wl_pointer_send_button(pointer->resource, serial, time, button, state);
            wl_pointer_send_frame(pointer->resource);
        }
    }
}

uint32_t owl_input_get_keycode(Owl_Input* input) {
    return input ? input->keycode : 0;
}

uint32_t owl_input_get_keysym(Owl_Input* input) {
    return input ? input->keysym : 0;
}

uint32_t owl_input_get_modifiers(Owl_Input* input) {
    return input ? input->modifiers : 0;
}

uint32_t owl_input_get_button(Owl_Input* input) {
    return input ? input->button : 0;
}

int owl_input_get_pointer_x(Owl_Input* input) {
    return input ? input->pointer_x : 0;
}

int owl_input_get_pointer_y(Owl_Input* input) {
    return input ? input->pointer_y : 0;
}
