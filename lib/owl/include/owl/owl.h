#ifndef OWL_H
#define OWL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Owl_Display Owl_Display;
typedef struct Owl_Window Owl_Window;
typedef struct Owl_Output Owl_Output;
typedef struct Owl_Input Owl_Input;

typedef enum {
    OWL_WINDOW_EVENT_CREATE,
    OWL_WINDOW_EVENT_DESTROY,
    OWL_WINDOW_EVENT_MAP,
    OWL_WINDOW_EVENT_UNMAP,
    OWL_WINDOW_EVENT_FOCUS,
    OWL_WINDOW_EVENT_UNFOCUS,
    OWL_WINDOW_EVENT_MOVE,
    OWL_WINDOW_EVENT_RESIZE,
    OWL_WINDOW_EVENT_FULLSCREEN,
    OWL_WINDOW_EVENT_TITLE_CHANGE,
    OWL_WINDOW_EVENT_REQUEST_MOVE,
    OWL_WINDOW_EVENT_REQUEST_RESIZE,
} Owl_Window_Event;

typedef enum {
    OWL_INPUT_KEY_PRESS,
    OWL_INPUT_KEY_RELEASE,
    OWL_INPUT_BUTTON_PRESS,
    OWL_INPUT_BUTTON_RELEASE,
    OWL_INPUT_POINTER_MOTION,
} Owl_Input_Event;

typedef enum {
    OWL_OUTPUT_EVENT_CONNECT,
    OWL_OUTPUT_EVENT_DISCONNECT,
    OWL_OUTPUT_EVENT_MODE_CHANGE,
} Owl_Output_Event;

typedef void (*Owl_Window_Callback)(Owl_Display* display, Owl_Window* window, void* data);
typedef void (*Owl_Input_Callback)(Owl_Display* display, Owl_Input* input, void* data);
typedef void (*Owl_Output_Callback)(Owl_Display* display, Owl_Output* output, void* data);

Owl_Display* owl_display_create(void);
void owl_display_destroy(Owl_Display* display);
void owl_display_run(Owl_Display* display);
void owl_display_terminate(Owl_Display* display);
const char* owl_display_get_socket_name(Owl_Display* display);
int owl_display_get_pointer_x(Owl_Display* display);
int owl_display_get_pointer_y(Owl_Display* display);

Owl_Window** owl_get_windows(Owl_Display* display, int* count);
void owl_window_focus(Owl_Window* window);
void owl_window_move(Owl_Window* window, int x, int y);
void owl_window_resize(Owl_Window* window, int width, int height);
void owl_window_close(Owl_Window* window);
void owl_window_set_fullscreen(Owl_Window* window, bool fullscreen);

int owl_window_get_x(Owl_Window* window);
int owl_window_get_y(Owl_Window* window);
int owl_window_get_width(Owl_Window* window);
int owl_window_get_height(Owl_Window* window);
const char* owl_window_get_title(Owl_Window* window);
const char* owl_window_get_app_id(Owl_Window* window);
bool owl_window_is_fullscreen(Owl_Window* window);
bool owl_window_is_focused(Owl_Window* window);

Owl_Output** owl_get_outputs(Owl_Display* display, int* count);
int owl_output_get_x(Owl_Output* output);
int owl_output_get_y(Owl_Output* output);
int owl_output_get_width(Owl_Output* output);
int owl_output_get_height(Owl_Output* output);
const char* owl_output_get_name(Owl_Output* output);

void owl_set_window_callback(Owl_Display* display, Owl_Window_Event type, Owl_Window_Callback callback, void* data);
void owl_set_input_callback(Owl_Display* display, Owl_Input_Event type, Owl_Input_Callback callback, void* data);
void owl_set_output_callback(Owl_Display* display, Owl_Output_Event type, Owl_Output_Callback callback, void* data);

uint32_t owl_input_get_keycode(Owl_Input* input);
uint32_t owl_input_get_keysym(Owl_Input* input);
uint32_t owl_input_get_modifiers(Owl_Input* input);
uint32_t owl_input_get_button(Owl_Input* input);
int owl_input_get_pointer_x(Owl_Input* input);
int owl_input_get_pointer_y(Owl_Input* input);

#define OWL_MOD_SHIFT   (1 << 0)
#define OWL_MOD_CTRL    (1 << 1)
#define OWL_MOD_ALT     (1 << 2)
#define OWL_MOD_SUPER   (1 << 3)

#endif
