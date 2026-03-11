#include "internal.h"

void owl_set_window_callback(
        Owl_Display* display,
        Owl_Window_Event type,
        Owl_Window_Callback callback,
        void* data
    ) {
    if (!display || type < 0 || type > OWL_WINDOW_EVENT_REQUEST_RESIZE) {
        return;
    }

    int count = display->window_callback_count[type];
    if (count >= OWL_MAX_CALLBACKS) {
        return;
    }

    display->window_callbacks[type][count].callback = callback;
    display->window_callbacks[type][count].data = data;
    display->window_callback_count[type]++;
}

void owl_set_input_callback(
        Owl_Display* display,
        Owl_Input_Event type,
        Owl_Input_Callback callback,
        void* data
    ) {
    if (!display || type < 0 || type > OWL_INPUT_POINTER_MOTION) {
        return;
    }

    int count = display->input_callback_count[type];
    if (count >= OWL_MAX_CALLBACKS) {
        return;
    }

    display->input_callbacks[type][count].callback = callback;
    display->input_callbacks[type][count].data = data;
    display->input_callback_count[type]++;
}

void owl_set_output_callback(
        Owl_Display* display,
        Owl_Output_Event type,
        Owl_Output_Callback callback,
        void* data
    ) {
    if (!display || type < 0 || type > OWL_OUTPUT_EVENT_MODE_CHANGE) {
        return;
    }

    int count = display->output_callback_count[type];
    if (count >= OWL_MAX_CALLBACKS) {
        return;
    }

    display->output_callbacks[type][count].callback = callback;
    display->output_callbacks[type][count].data = data;
    display->output_callback_count[type]++;
}

void owl_invoke_window_callback(Owl_Display* display, Owl_Window_Event type, Owl_Window* window) {
    if (!display || type < 0 || type > OWL_WINDOW_EVENT_REQUEST_RESIZE) {
        return;
    }

    int count = display->window_callback_count[type];
    for (int index = 0; index < count; index++) {
        Window_Callback_Entry* entry = &display->window_callbacks[type][index];
        if (entry->callback) {
            entry->callback(display, window, entry->data);
        }
    }
}

bool owl_invoke_input_callback(Owl_Display* display, Owl_Input_Event type, Owl_Input* input) {
    if (!display || type < 0 || type > OWL_INPUT_POINTER_MOTION) {
        return false;
    }

    bool handled = false;
    int count = display->input_callback_count[type];
    for (int index = 0; index < count; index++) {
        Input_Callback_Entry* entry = &display->input_callbacks[type][index];
        if (entry->callback) {
            if (entry->callback(display, input, entry->data)) {
                handled = true;
            }
        }
    }
    return handled;
}

void owl_invoke_output_callback(Owl_Display* display, Owl_Output_Event type, Owl_Output* output) {
    if (!display || type < 0 || type > OWL_OUTPUT_EVENT_MODE_CHANGE) {
        return;
    }

    int count = display->output_callback_count[type];
    for (int index = 0; index < count; index++) {
        Output_Callback_Entry* entry = &display->output_callbacks[type][index];
        if (entry->callback) {
            entry->callback(display, output, entry->data);
        }
    }
}

void owl_set_layer_surface_callback(
        Owl_Display* display,
        Owl_Layer_Surface_Event type,
        Owl_Layer_Surface_Callback callback,
        void* data
    ) {
    if (!display || type < 0 || type > OWL_LAYER_SURFACE_EVENT_UNMAP) {
        return;
    }

    int count = display->layer_surface_callback_count[type];
    if (count >= OWL_MAX_CALLBACKS) {
        return;
    }

    display->layer_surface_callbacks[type][count].callback = callback;
    display->layer_surface_callbacks[type][count].data = data;
    display->layer_surface_callback_count[type]++;
}

void owl_invoke_layer_surface_callback(
        Owl_Display* display,
        Owl_Layer_Surface_Event type,
        Owl_Layer_Surface* surface
    ) {
    if (!display || type < 0 || type > OWL_LAYER_SURFACE_EVENT_UNMAP) {
        return;
    }

    int count = display->layer_surface_callback_count[type];
    for (int index = 0; index < count; index++) {
        Layer_Surface_Callback_Entry* entry = &display->layer_surface_callbacks[type][index];
        if (entry->callback) {
            entry->callback(display, surface, entry->data);
        }
    }
}
