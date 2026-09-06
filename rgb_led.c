#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define RGB_LED_FINE_STEP 1U
#define RGB_LED_FAST_STEP 5U

typedef enum {
    RgbLedChannelRed = 0,
    RgbLedChannelGreen,
    RgbLedChannelBlue,
} RgbLedChannel;

typedef struct {
    const char* name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} RgbLedPreset;

static const RgbLedPreset rgb_led_presets[] = {
    {.name = "RED", .red = 255, .green = 0, .blue = 0},
    {.name = "GREEN", .red = 0, .green = 255, .blue = 0},
    {.name = "BLUE", .red = 0, .green = 0, .blue = 255},
    {.name = "YELLOW", .red = 255, .green = 255, .blue = 0},
    {.name = "CYAN", .red = 0, .green = 255, .blue = 255},
    {.name = "MAGENTA", .red = 255, .green = 0, .blue = 255},
    {.name = "WHITE", .red = 255, .green = 255, .blue = 255},
    {.name = "ORANGE", .red = 255, .green = 80, .blue = 0},
    {.name = "PURPLE", .red = 128, .green = 0, .blue = 255},
};

#define RGB_LED_PRESET_COUNT (sizeof(rgb_led_presets) / sizeof(rgb_led_presets[0]))
#define RGB_LED_PRESET_CUSTOM RGB_LED_PRESET_COUNT

typedef struct {
    InputEvent input;
} RgbLedEvent;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    NotificationApp* notification;
    FuriMessageQueue* event_queue;
    FuriMutex* mutex;

    uint8_t red;
    uint8_t green;
    uint8_t blue;
    RgbLedChannel selected_channel;
    size_t selected_preset;
    bool led_enabled;
} RgbLedApp;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    RgbLedChannel selected_channel;
    size_t selected_preset;
    bool led_enabled;
} RgbLedSnapshot;

static void rgb_led_get_snapshot(RgbLedApp* app, RgbLedSnapshot* snapshot) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

    snapshot->red = app->red;
    snapshot->green = app->green;
    snapshot->blue = app->blue;
    snapshot->selected_channel = app->selected_channel;
    snapshot->selected_preset = app->selected_preset;
    snapshot->led_enabled = app->led_enabled;

    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void rgb_led_apply(RgbLedApp* app) {
    RgbLedSnapshot snapshot;
    rgb_led_get_snapshot(app, &snapshot);

    const uint8_t red = snapshot.led_enabled ? snapshot.red : 0;
    const uint8_t green = snapshot.led_enabled ? snapshot.green : 0;
    const uint8_t blue = snapshot.led_enabled ? snapshot.blue : 0;

    const NotificationMessage message_red = {
        .type = NotificationMessageTypeLedRed,
        .data.led.value = red,
    };
    const NotificationMessage message_green = {
        .type = NotificationMessageTypeLedGreen,
        .data.led.value = green,
    };
    const NotificationMessage message_blue = {
        .type = NotificationMessageTypeLedBlue,
        .data.led.value = blue,
    };
    const NotificationSequence sequence = {
        &message_red,
        &message_green,
        &message_blue,
        &message_do_not_reset,
        NULL,
    };

    /*
     * The messages above live on this stack. The blocking call guarantees that
     * all three values are consumed before the function returns and before a
     * later key press can overwrite them.
     */
    notification_message_block(app->notification, &sequence);
}

static void rgb_led_draw_channel(
    Canvas* canvas,
    uint8_t x,
    char label,
    uint8_t value,
    bool selected) {
    char text[8];
    snprintf(text, sizeof(text), "%c:%3u", label, (unsigned int)value);

    if(selected) {
        canvas_draw_box(canvas, x, 13, 41, 18);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_frame(canvas, x, 13, 41, 18);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, x + 20, 22, AlignCenter, AlignCenter, text);
    canvas_set_color(canvas, ColorBlack);
}

static void rgb_led_draw_callback(Canvas* canvas, void* context) {
    RgbLedApp* app = context;
    RgbLedSnapshot snapshot;
    rgb_led_get_snapshot(app, &snapshot);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "RGB LED 1.0");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        126,
        9,
        AlignRight,
        AlignBottom,
        snapshot.led_enabled ? "ON" : "OFF");

    rgb_led_draw_channel(
        canvas,
        1,
        'R',
        snapshot.red,
        snapshot.selected_channel == RgbLedChannelRed);
    rgb_led_draw_channel(
        canvas,
        43,
        'G',
        snapshot.green,
        snapshot.selected_channel == RgbLedChannelGreen);
    rgb_led_draw_channel(
        canvas,
        85,
        'B',
        snapshot.blue,
        snapshot.selected_channel == RgbLedChannelBlue);

    const char* preset_name = "CUSTOM";
    if(snapshot.selected_preset < RGB_LED_PRESET_COUNT) {
        preset_name = rgb_led_presets[snapshot.selected_preset].name;
    }

    char preset_text[24];
    snprintf(preset_text, sizeof(preset_text), "Preset: %s", preset_name);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 42, preset_text);

    const uint8_t selected_value =
        (snapshot.selected_channel == RgbLedChannelRed)     ? snapshot.red :
        (snapshot.selected_channel == RgbLedChannelGreen) ? snapshot.green :
                                                            snapshot.blue;
    const uint8_t bar_width = (uint16_t)selected_value * 122U / 255U;
    canvas_draw_frame(canvas, 2, 46, 124, 7);
    if(bar_width > 0) {
        canvas_draw_box(canvas, 3, 47, bar_width, 5);
    }

    canvas_draw_str_aligned(
        canvas, 64, 63, AlignCenter, AlignBottom, "<>kanal  ^v+/-  OK preset");
}

static void rgb_led_input_callback(InputEvent* input_event, void* context) {
    RgbLedApp* app = context;
    const RgbLedEvent event = {.input = *input_event};
    furi_message_queue_put(app->event_queue, &event, 0);
}

static uint8_t rgb_led_adjust_value(uint8_t value, bool increase, uint8_t step) {
    if(increase) {
        const uint16_t next = (uint16_t)value + step;
        return (next > UINT8_MAX) ? UINT8_MAX : (uint8_t)next;
    }

    return (value < step) ? 0 : (uint8_t)(value - step);
}

static bool rgb_led_adjust_selected(RgbLedApp* app, bool increase, uint8_t step) {
    uint8_t* selected_value = &app->red;
    if(app->selected_channel == RgbLedChannelGreen) {
        selected_value = &app->green;
    } else if(app->selected_channel == RgbLedChannelBlue) {
        selected_value = &app->blue;
    }

    const uint8_t old_value = *selected_value;
    *selected_value = rgb_led_adjust_value(old_value, increase, step);
    if(*selected_value != old_value) {
        app->selected_preset = RGB_LED_PRESET_CUSTOM;
        return true;
    }

    return false;
}

static void rgb_led_select_next_preset(RgbLedApp* app) {
    if(app->selected_preset >= RGB_LED_PRESET_COUNT) {
        app->selected_preset = 0;
    } else {
        app->selected_preset = (app->selected_preset + 1U) % RGB_LED_PRESET_COUNT;
    }

    const RgbLedPreset* preset = &rgb_led_presets[app->selected_preset];
    app->red = preset->red;
    app->green = preset->green;
    app->blue = preset->blue;
    app->led_enabled = true;
}

static bool rgb_led_process_event(RgbLedApp* app, const InputEvent* input, bool* running) {
    bool redraw = false;
    bool apply = false;

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

    if(input->type == InputTypePress || input->type == InputTypeRepeat) {
        switch(input->key) {
        case InputKeyLeft:
            if(input->type == InputTypePress) {
                app->selected_channel =
                    (app->selected_channel == RgbLedChannelRed) ? RgbLedChannelBlue :
                                                                  app->selected_channel - 1;
                redraw = true;
            }
            break;

        case InputKeyRight:
            if(input->type == InputTypePress) {
                app->selected_channel =
                    (app->selected_channel == RgbLedChannelBlue) ? RgbLedChannelRed :
                                                                   app->selected_channel + 1;
                redraw = true;
            }
            break;

        case InputKeyUp:
            apply = rgb_led_adjust_selected(
                app,
                true,
                input->type == InputTypeRepeat ? RGB_LED_FAST_STEP : RGB_LED_FINE_STEP);
            redraw = apply;
            break;

        case InputKeyDown:
            apply = rgb_led_adjust_selected(
                app,
                false,
                input->type == InputTypeRepeat ? RGB_LED_FAST_STEP : RGB_LED_FINE_STEP);
            redraw = apply;
            break;

        case InputKeyBack:
            if(input->type == InputTypePress) {
                *running = false;
            }
            break;

        default:
            break;
        }
    } else if(input->type == InputTypeShort && input->key == InputKeyOk) {
        rgb_led_select_next_preset(app);
        redraw = true;
        apply = true;
    } else if(input->type == InputTypeLong && input->key == InputKeyOk) {
        app->led_enabled = !app->led_enabled;
        redraw = true;
        apply = true;
    }

    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(apply) {
        rgb_led_apply(app);
    }

    return redraw;
}

int32_t rgb_led_app(void* context) {
    UNUSED(context);

    RgbLedApp app = {
        .red = 255,
        .green = 0,
        .blue = 0,
        .selected_channel = RgbLedChannelRed,
        .selected_preset = 0,
        .led_enabled = true,
    };

    app.event_queue = furi_message_queue_alloc(8, sizeof(RgbLedEvent));
    app.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app.view_port = view_port_alloc();
    app.gui = furi_record_open(RECORD_GUI);
    app.notification = furi_record_open(RECORD_NOTIFICATION);

    furi_check(app.event_queue);
    furi_check(app.mutex);
    furi_check(app.view_port);
    furi_check(app.gui);
    furi_check(app.notification);

    view_port_draw_callback_set(app.view_port, rgb_led_draw_callback, &app);
    view_port_input_callback_set(app.view_port, rgb_led_input_callback, &app);
    gui_add_view_port(app.gui, app.view_port, GuiLayerFullscreen);

    rgb_led_apply(&app);
    view_port_update(app.view_port);

    bool running = true;
    while(running) {
        RgbLedEvent event;
        if(furi_message_queue_get(app.event_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(rgb_led_process_event(&app, &event.input, &running)) {
                view_port_update(app.view_port);
            }
        }
    }

    notification_message_block(app.notification, &sequence_reset_rgb);

    gui_remove_view_port(app.gui, app.view_port);
    view_port_free(app.view_port);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    furi_mutex_free(app.mutex);
    furi_message_queue_free(app.event_queue);

    return 0;
}
