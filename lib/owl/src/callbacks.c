#include "internal.h"

void owl_set_window_callback(
        owl_display* display,
        owl_window_event type,
        owl_window_callback callback,
        void *data
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
        owl_display *display,
        owl_input_event type,
        owl_input_callback callback,
        void *data
    ) {
    if (!display || type < 0 || type > OWL_INPUT_EVENT_POINTER_MOTION) {
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
        owl_display *display,
        owl_output_event type,
        owl_output_callback callback,
        void *data
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

void owl_invoke_window_callback(
        owl_display *display,
        owl_window_event type,
        owl_window *window
    ) {
    if (!display || type < 0 || type > OWL_WINDOW_EVENT_REQUEST_RESIZE) {
        return;
    }

    int count = display->window_callback_count[type];
    for (int index = 0; index < count; index++) {
        window_callback_entry* entry = &display->window_callbacks[type][index];
        if (entry->callback) {
            entry->callback(display, window, entry->data);
        }
    }
}

bool owl_invoke_input_callback(owl_display* display, owl_input_event type, owl_input* input) {
    if (!display || type < 0 || type > OWL_INPUT_EVENT_POINTER_MOTION) {
        return false;
    }

    bool handled = false;
    int count = display->input_callback_count[type];
    for (int index = 0; index < count; index++) {
        input_callback_entry* entry = &display->input_callbacks[type][index];
        if (entry->callback) {
            if (entry->callback(display, input, entry->data)) {
                handled = true;
            }
        }
    }
    return handled;
}

void owl_invoke_output_callback(owl_display* display, owl_output_event type, owl_output* output) {
    if (!display || type < 0 || type > OWL_OUTPUT_EVENT_MODE_CHANGE) {
        return;
    }

    int count = display->output_callback_count[type];
    for (int index = 0; index < count; index++) {
        output_callback_entry* entry = &display->output_callbacks[type][index];
        if (entry->callback) {
            entry->callback(display, output, entry->data);
        }
    }
}

void owl_set_layer_surface_callback(
        owl_display* display,
        owl_layer_surface_event type,
        owl_layer_surface_callback callback,
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
        owl_display* display,
        owl_layer_surface_event type,
        owl_layer_surface* surface
    ) {
    if (!display || type < 0 || type > OWL_LAYER_SURFACE_EVENT_UNMAP) {
        return;
    }

    int count = display->layer_surface_callback_count[type];
    for (int index = 0; index < count; index++) {
        layer_surface_callback_entry* entry = &display->layer_surface_callbacks[type][index];
        if (entry->callback) {
            entry->callback(display, surface, entry->data);
        }
    }
}
