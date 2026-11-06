#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>
#include <stm32wbxx_ll_tim.h>
#include "pmr_ui.h"
#include "pmr_preferences.h"
#include "pmr_cli.h"
#include "pmr_tts.h"
#include "pmr_speak_core.h"
#include "pmr_speak_plugin.h"
#include <flipper_application/flipper_application.h>

#define SPEAK_PATH     PMR_SETTINGS_DIR "/speak.txt"
#define SPEAK_MAX_TX   60000U // the same per-transmission limit as push-to-talk
#define SPEAK_ROWS     8U
#define SPEAK_BUSY_MS  300U
#define SPEAK_FILE_MAX 200U

typedef enum {
    StateMenu,
    StatePreview,
    StateChecking,
    StateBusy,
    StateSending,
    StatePause,
} SpeakState;
typedef enum {
    RequestNone,
    RequestSend,
    RequestPreview,
    RequestStop,
} SpeakRequest;

typedef struct {
    ViewPort* view;
    Gui* gui;
    FuriMessageQueue* keys;
    Storage* storage;
    PmrPreferences prefs;
    PmrSpeakSettings settings;
    PmrTts* tts;
    PmrSpeakTx* tx;
    ViewDispatcher* editor;
    char draft[PMR_SPEAK_TEXT_MAX + 1];
    const char* notice;
    bool edit_done, running, stop, speaker_owned;
    volatile uint32_t request;
    volatile uint32_t state, sent, pause_left, tx_count, underruns, tx_ms, audio_ms, synth_cycles;
    volatile int32_t rssi, trigger_dbm;
    volatile uint32_t speaker_ticks, speaker_pin_high, speaker_low, speaker_high;
    uint32_t selected, top, volume;
} Speak;

// The tested radio preset, with the receive and transmit register changes from
// the browser link already applied. PA table follows the 0, 0 terminator.
static const uint8_t preset_listen[] = {0x02, 0x0D, 0x0B, 0x06, 0x08, 0x32, 0x07, 0x00, 0x15, 0x04,
                                        0x14, 0x00, 0x13, 0x02, 0x12, 0x00, 0x11, 0x83, 0x10, 0xFA,
                                        0x18, 0x18, 0x19, 0x00, 0x1D, 0x90, 0x1C, 0x00, 0x1B, 0x03,
                                        0x20, 0xFB, 0x22, 0x10, 0x21, 0x56, 0,    0,    0,    0,
                                        0,    0,    0,    0,    0,    0};
static const uint8_t preset_transmit[] = {
    0x02, 0x0D, 0x0B, 0x06, 0x08, 0x32, 0x07, 0x00, 0x15, 0x04, 0x14, 0x00, 0x13, 0x02, 0x12, 0x00,
    0x11, 0xE4, 0x10, 0x6A, 0x18, 0x18, 0x19, 0x16, 0x1D, 0x90, 0x1C, 0x00, 0x1B, 0x07, 0x20, 0xFB,
    0x22, 0x10, 0x21, 0x56, 0,    0,    0x60, 0,    0,    0,    0,    0,    0,    0};

static void settings_load(Speak* s) {
    pmr_speak_defaults(&s->settings);
    char data[SPEAK_FILE_MAX + 1];
    File* file = storage_file_alloc(s->storage);
    if(storage_file_open(file, SPEAK_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t n = storage_file_read(file, data, SPEAK_FILE_MAX);
        data[n] = 0;
        pmr_speak_decode(data, &s->settings);
    }
    storage_file_close(file);
    storage_file_free(file);
}
static void settings_save(Speak* s) {
    char data[SPEAK_FILE_MAX + 1];
    size_t n = pmr_speak_encode(&s->settings, data, sizeof(data));
    storage_common_mkdir(s->storage, PMR_SETTINGS_DIR);
    File* file = storage_file_alloc(s->storage);
    bool ok = n && storage_file_open(file, SPEAK_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              storage_file_write(file, data, n) == n;
    storage_file_close(file);
    storage_file_free(file);
    bool prefs = pmr_preferences_save(s->storage, &s->prefs);
    if(!ok || !prefs) s->notice = "SD error";
}

static void row_text(const Speak* s, uint32_t row, char* out, size_t size) {
    static const char* const speeds[] = {"Slow", "Normal", "Fast"};
    static const char* const pitches[] = {"Low", "Mid", "High"};
    out[0] = 0;
    if(row == 0) {
        size_t n = strlen(s->settings.text);
        snprintf(out, size, "%.12s%s", s->settings.text, n > 12 ? ".." : "");
    } else if(row == 3)
        snprintf(out, size, "< %u >", s->settings.repeats);
    else if(row == 4)
        snprintf(out, size, "< %u s >", s->settings.pause_s);
    else if(row == 5)
        snprintf(out, size, "< %s >", speeds[s->settings.speed]);
    else if(row == 6)
        snprintf(out, size, "< %s >", pitches[s->settings.pitch]);
    else if(row == 7)
        snprintf(out, size, "< %02u >", s->prefs.channel);
}
static void draw(Canvas* c, void* context) {
    Speak* s = context;
    char detail[32];
    uint32_t state = s->state;
    if(state == StateMenu) {
        static const char* const labels[SPEAK_ROWS] = {
            "Message",
            "Send on radio",
            "Hear on Flipper",
            "Repeats",
            "Pause",
            "Speed",
            "Pitch",
            "Channel"};
        pmr_ui_header(c, "Speak text", s->notice ? s->notice : "");
        for(uint32_t i = 0; i < 4; i++) {
            uint32_t row = s->top + i;
            row_text(s, row, detail, sizeof(detail));
            pmr_ui_row(c, i, row == s->selected, true, labels[row], detail);
        }
        pmr_ui_scroll(c, s->selected, SPEAK_ROWS);
        return;
    }
    if(state == StatePreview) {
        pmr_ui_header(c, "Speak text", "Speaker");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str(c, 3, 25, "Playing on the Flipper.");
        canvas_draw_str(c, 3, 36, "Nothing is transmitted.");
        elements_button_center(c, "Stop");
        return;
    }
    const char* title = state == StateBusy ? "Busy" : state == StatePause ? "Waiting" : "Sending";
    if(state == StatePause)
        snprintf(detail, sizeof(detail), "Next in %lu s", s->pause_left);
    else
        snprintf(
            detail,
            sizeof(detail),
            "%lu of %u",
            s->sent + (state == StateSending ? 1 : 0),
            s->settings.repeats);
    pmr_ui_radio(c, "Speak text", "TX", s->prefs.channel, s->rssi, title, detail);
    elements_button_center(c, "Stop");
}
static void input(InputEvent* event, void* context) {
    Speak* s = context;
    furi_message_queue_put(s->keys, event, 0);
}

// Waits while watching for Stop. Returns true once stopped.
static bool stop_wait(Speak* s, uint32_t ms) {
    InputEvent event;
    if(furi_message_queue_get(s->keys, &event, ms) == FuriStatusOk &&
       event.type == InputTypeShort && (event.key == InputKeyOk || event.key == InputKeyBack))
        s->stop = true;
    if(s->request == RequestStop) {
        s->request = RequestNone;
        s->stop = true;
    }
    return s->stop;
}

static void synth_fill(Speak* s) {
    PmrSpeakTx* tx = s->tx;
    int16_t block[64];
    uint32_t started = furi_hal_cortex_timer_get(0).start;
    while(!tx->finished) {
        uint32_t space = pmr_speak_tx_space(tx);
        if(space > 64) space = 64;
        if(!space) break;
        size_t n = pmr_tts_render(s->tts, block, space);
        if(!n)
            tx->finished = true;
        else
            pmr_speak_tx_push(tx, block, n);
    }
    s->synth_cycles += furi_hal_cortex_timer_get(0).start - started;
}

static bool synth_load(Speak* s) {
    bool ok = pmr_tts_load(
        s->tts,
        s->settings.text,
        pmr_speak_speeds[s->settings.speed],
        pmr_speak_pitches[s->settings.pitch]);
    if(!ok) s->notice = "Nothing to say";
    return ok;
}

static void speaker_irq(void* context) {
    if(!LL_TIM_IsActiveFlag_UPDATE(TIM2)) return;
    LL_TIM_ClearFlag_UPDATE(TIM2);
    Speak* s = context;
    int16_t pcm = 0;
    pmr_speak_tx_pop(s->tx, &pcm);
    uint32_t duty = pmr_speak_speaker_duty(pcm, s->volume);
    TIM16->CCR1 = duty;
    s->speaker_ticks++;
    if(furi_hal_gpio_read(&gpio_speaker)) s->speaker_pin_high++;
    if(duty < s->speaker_low) s->speaker_low = duty;
    if(duty > s->speaker_high) s->speaker_high = duty;
}
static void speaker_start(Speak* s) {
    furi_hal_gpio_init_ex(
        &gpio_speaker, GpioModeAltFunctionPushPull, GpioPullNo, GpioSpeedLow, GpioAltFn14TIM16);
    TIM16->CR1 = 0;
    TIM16->CR2 = 0;
    TIM16->DIER = 0;
    TIM16->CCER = 0;
    TIM16->CCMR1 = TIM_CCMR1_OC1PE | (6U << TIM_CCMR1_OC1M_Pos);
    TIM16->PSC = 0;
    TIM16->ARR = 1023;
    TIM16->RCR = 0;
    TIM16->CCR1 = 512;
    TIM16->EGR = TIM_EGR_UG;
    TIM16->SR = 0;
    TIM16->CCER = TIM_CCER_CC1E;
    TIM16->BDTR = 0;
    TIM16->CR1 = TIM_CR1_CEN | TIM_CR1_ARPE;
    furi_hal_bus_enable(FuriHalBusTIM2);
    LL_TIM_InitTypeDef timer = {0};
    timer.Prescaler = 63;
    timer.Autoreload = 166; // 1 MHz / 167 = 5988 Hz, inaudibly below 6 kHz
    LL_TIM_Init(TIM2, &timer);
    LL_TIM_SetClockSource(TIM2, LL_TIM_CLOCKSOURCE_INTERNAL);
    LL_TIM_DisableARRPreload(TIM2);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, speaker_irq, s);
    LL_TIM_ClearFlag_UPDATE(TIM2);
    LL_TIM_EnableIT_UPDATE(TIM2);
    LL_TIM_EnableAllOutputs(TIM16);
    LL_TIM_EnableCounter(TIM2);
}
static void speaker_stop(void) {
    LL_TIM_DisableIT_UPDATE(TIM2);
    LL_TIM_DisableCounter(TIM2);
    LL_TIM_ClearFlag_UPDATE(TIM2);
    LL_TIM_DisableAllOutputs(TIM16);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, NULL, NULL);
    furi_hal_bus_disable(FuriHalBusTIM2);
}
static void preview(Speak* s) {
    if(!synth_load(s)) return;
    if(!furi_hal_speaker_acquire(1000)) {
        s->notice = "Speaker busy";
        return;
    }
    s->stop = false;
    s->state = StatePreview;
    view_port_update(s->view);
    pmr_speak_tx_reset(s->tx);
    synth_fill(s);
    s->speaker_ticks = s->speaker_pin_high = 0;
    s->speaker_low = s->speaker_high = 512;
    speaker_start(s);
    while(!stop_wait(s, 10)) {
        synth_fill(s);
        if(s->tx->finished && s->tx->head == s->tx->tail) break;
    }
    speaker_stop();
    furi_hal_speaker_release();
    s->notice = NULL;
    s->state = StateMenu;
}

static void radio_listen(Speak* s) {
    furi_hal_subghz_idle();
    furi_hal_subghz_load_custom_preset(preset_listen);
    furi_hal_subghz_set_frequency_and_path(pmr_channel_hz(s->prefs.channel));
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_subghz_rx();
    furi_delay_ms(30);
}
static int32_t radio_rssi(void) {
    return (int32_t)furi_hal_subghz_get_rssi();
}
// Listen-before-talk, using the browser link's noise-floor rule. A channel that
// is already loud when this runs keeps the fixed -90 dBm threshold.
static void channel_calibrate(Speak* s) {
    radio_listen(s);
    int32_t floor = radio_rssi();
    s->rssi = floor;
    s->trigger_dbm = floor < -80 ? CLAMP(floor + 10, -80, -100) : -90;
}
// Returns false when stopped. Waits for as long as someone else is talking.
static bool channel_wait(Speak* s) {
    radio_listen(s);
    while(true) {
        s->rssi = radio_rssi();
        if(s->rssi <= s->trigger_dbm) return true;
        s->state = StateBusy;
        if(stop_wait(s, SPEAK_BUSY_MS)) return false;
    }
}

static LevelDuration tx_yield(void* context) {
    bool level;
    uint32_t duration;
    if(!pmr_speak_tx_next(context, &level, &duration)) return level_duration_reset();
    // API 88 HAL performs the timer ARR correction.
    return level_duration_make(level, duration);
}
// One complete message. Returns false when it did not finish normally.
static bool transmit(Speak* s) {
    uint32_t hz = pmr_channel_hz(s->prefs.channel);
    if(!furi_hal_subghz_is_tx_allowed(hz)) {
        s->notice = "TX blocked";
        return false;
    }
    pmr_tts_rewind(s->tts);
    pmr_speak_tx_reset(s->tx);
    synth_fill(s);
    furi_hal_subghz_idle();
    furi_hal_subghz_load_custom_preset(preset_transmit);
    furi_hal_subghz_set_frequency_and_path(hz);
    furi_hal_subghz_set_async_mirror_pin(NULL);
    if(!furi_hal_subghz_start_async_tx(tx_yield, s->tx)) {
        s->notice = "TX blocked";
        return false;
    }
    s->state = StateSending;
    s->tx_count++;
    uint32_t started = furi_get_tick();
    bool expired = false;
    while(!furi_hal_subghz_is_async_tx_complete() && !stop_wait(s, 10)) {
        synth_fill(s);
        if(furi_get_tick() - started > SPEAK_MAX_TX) {
            expired = true;
            break;
        }
    }
    bool complete = furi_hal_subghz_is_async_tx_complete();
    furi_hal_subghz_stop_async_tx();
    furi_hal_subghz_idle();
    s->tx_ms = furi_get_tick() - started;
    s->audio_ms = PMR_TX_LEAD_MS + s->tx->tail / (PMR_TTS_RATE / 1000U);
    s->underruns += s->tx->underruns;
    if(expired) s->notice = "Time limit";
    return complete;
}
static void send(Speak* s) {
    settings_save(s);
    s->notice = NULL;
    if(!synth_load(s)) return;
    s->stop = false;
    s->sent = 0;
    s->underruns = 0;
    s->synth_cycles = 0;
    s->state = StateChecking;
    view_port_update(s->view);
    channel_calibrate(s);
    while(s->sent < s->settings.repeats) {
        s->state = StateChecking;
        if(!channel_wait(s) || !transmit(s)) break;
        s->sent = s->sent + 1;
        if(s->sent == s->settings.repeats) break;
        // Leave the channel free for replies between repeats.
        s->state = StatePause;
        bool stopped = false;
        for(uint32_t left = s->settings.pause_s * 10U; left && !stopped; left--) {
            s->pause_left = (left + 9U) / 10U;
            stopped = stop_wait(s, 100);
        }
        if(stopped) break;
    }
    furi_hal_subghz_idle();
    if(!s->notice) s->notice = s->sent == s->settings.repeats ? "Sent" : "Stopped";
    s->state = StateMenu;
}

static void edit_result(void* context) {
    Speak* s = context;
    s->edit_done = true;
    view_dispatcher_stop(s->editor);
}
static bool edit_back(void* context) {
    Speak* s = context;
    view_dispatcher_stop(s->editor);
    return true;
}
static void edit(Speak* s) {
    view_port_enabled_set(s->view, false);
    gui_remove_view_port(s->gui, s->view);
    TextInput* text = text_input_alloc();
    s->editor = view_dispatcher_alloc();
    s->edit_done = false;
    strcpy(s->draft, s->settings.text);
    view_dispatcher_set_event_callback_context(s->editor, s);
    view_dispatcher_set_navigation_event_callback(s->editor, edit_back);
    view_dispatcher_add_view(s->editor, 0, text_input_get_view(text));
    view_dispatcher_attach_to_gui(s->editor, s->gui, ViewDispatcherTypeFullscreen);
    text_input_set_header_text(text, "Message to speak");
    text_input_set_minimum_length(text, 1);
    // The old message starts selected: the first key replaces it, Save keeps it.
    text_input_set_result_callback(text, edit_result, s, s->draft, sizeof(s->draft), true);
    view_dispatcher_switch_to_view(s->editor, 0);
    view_dispatcher_run(s->editor);
    view_dispatcher_remove_view(s->editor, 0);
    view_dispatcher_free(s->editor);
    s->editor = NULL;
    text_input_free(text);
    if(s->edit_done) {
        strcpy(s->settings.text, s->draft);
        settings_save(s);
    }
    gui_add_view_port(s->gui, s->view, GuiLayerFullscreen);
    view_port_enabled_set(s->view, true);
    furi_message_queue_reset(s->keys);
}

static void adjust(Speak* s, int32_t delta) {
    PmrSpeakSettings* v = &s->settings;
    if(s->selected == 3)
        v->repeats = (v->repeats + PMR_SPEAK_REPEATS_MAX - 1 + delta) % PMR_SPEAK_REPEATS_MAX + 1;
    else if(s->selected == 4)
        v->pause_s = (v->pause_s + PMR_SPEAK_PAUSE_MAX - 1 + delta) % PMR_SPEAK_PAUSE_MAX + 1;
    else if(s->selected == 5)
        v->speed = (v->speed + 3 + delta) % 3;
    else if(s->selected == 6)
        v->pitch = (v->pitch + 3 + delta) % 3;
    else if(s->selected == 7)
        s->prefs.channel = (s->prefs.channel + 15 + delta) % 16 + 1;
}
static void select_row(Speak* s, uint32_t row) {
    s->selected = row;
    if(row < s->top) s->top = row;
    if(row >= s->top + 4) s->top = row - 3;
}
static void key_handle(Speak* s, const InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return;
    bool press = event->type == InputTypeShort;
    s->notice = NULL;
    if(event->key == InputKeyBack && press)
        s->running = false;
    else if(event->key == InputKeyUp)
        select_row(s, (s->selected + SPEAK_ROWS - 1) % SPEAK_ROWS);
    else if(event->key == InputKeyDown)
        select_row(s, (s->selected + 1) % SPEAK_ROWS);
    else if(event->key == InputKeyLeft)
        adjust(s, -1);
    else if(event->key == InputKeyRight)
        adjust(s, 1);
    else if(event->key == InputKeyOk && press) {
        if(s->selected == 0)
            edit(s);
        else if(s->selected == 1)
            send(s);
        else if(s->selected == 2)
            preview(s);
        else
            adjust(s, 1);
    }
}

// Test hook for the host tools: lets a script run what the buttons run.
static void cli_command(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    Speak* s = context;
    const char* text = furi_string_get_cstr(args);
    bool idle = s->state == StateMenu;
    if(!strncmp(text, "text ", 5) && idle) {
        strncpy(s->settings.text, text + 5, PMR_SPEAK_TEXT_MAX);
        s->settings.text[PMR_SPEAK_TEXT_MAX] = 0;
    } else if(!strncmp(text, "repeat ", 7) && idle) {
        const char* p = text + 7;
        uint32_t repeats = 0, pause = 0;
        if(pmr_speak_number(&p, &repeats) && pmr_speak_number(&p, &pause) && repeats >= 1 &&
           repeats <= PMR_SPEAK_REPEATS_MAX && pause >= 1 && pause <= PMR_SPEAK_PAUSE_MAX) {
            s->settings.repeats = repeats;
            s->settings.pause_s = pause;
        }
    } else if(!strcmp(text, "send") && idle)
        s->request = RequestSend;
    else if(!strcmp(text, "preview") && idle)
        s->request = RequestPreview;
    else if(!strcmp(text, "stop"))
        s->request = RequestStop;
    printf(
        "state=%lu channel=%u sent=%lu repeats=%u tx_count=%lu tx_ms=%lu audio_ms=%lu underruns=%lu synth_cycles=%lu rssi=%ld trigger=%ld volume=%lu speaker_ticks=%lu pin_high=%lu duty=%lu-%lu notice=%s text=%s\r\n",
        s->state,
        s->prefs.channel,
        s->sent,
        s->settings.repeats,
        s->tx_count,
        s->tx_ms,
        s->audio_ms,
        s->underruns,
        s->synth_cycles,
        s->rssi,
        s->trigger_dbm,
        s->volume,
        s->speaker_ticks,
        s->speaker_pin_high,
        s->speaker_low,
        s->speaker_high,
        s->notice ? s->notice : "",
        s->settings.text);
}

static int32_t speak_app(void* args) {
    UNUSED(args);
    Speak* s = malloc(sizeof(Speak));
    memset(s, 0, sizeof(*s));
    s->tts = malloc(sizeof(PmrTts));
    s->tx = malloc(sizeof(PmrSpeakTx));
    s->storage = furi_record_open(RECORD_STORAGE);
    pmr_preferences_load(s->storage, &s->prefs);
    settings_load(s);
    s->volume = s->prefs.volume;
    s->running = true;
    s->rssi = -127;
    s->keys = furi_message_queue_alloc(8, sizeof(InputEvent));
    s->gui = furi_record_open(RECORD_GUI);
    s->view = view_port_alloc();
    view_port_draw_callback_set(s->view, draw, s);
    view_port_input_callback_set(s->view, input, s);
    gui_add_view_port(s->gui, s->view, GuiLayerFullscreen);
    CliRegistry* cli = furi_record_open(RECORD_CLI);
    cli_registry_add_command(cli, "pmr_speak", CliCommandFlagParallelSafe, cli_command, s);
    furi_hal_power_insomnia_enter();
    furi_hal_subghz_reset();
    furi_hal_subghz_idle();
    uint32_t next_draw = 0;
    while(s->running) {
        InputEvent event;
        if(furi_message_queue_get(s->keys, &event, 50) == FuriStatusOk) {
            key_handle(s, &event);
            view_port_update(s->view);
        }
        uint32_t request = s->request;
        s->request = RequestNone;
        if(request == RequestSend)
            send(s);
        else if(request == RequestPreview)
            preview(s);
        if(request) view_port_update(s->view);
        if(furi_get_tick() >= next_draw) {
            view_port_update(s->view);
            next_draw = furi_get_tick() + 150;
        }
    }
    settings_save(s);
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    furi_hal_power_insomnia_exit();
    cli_registry_delete_command(cli, "pmr_speak");
    furi_record_close(RECORD_CLI);
    view_port_enabled_set(s->view, false);
    gui_remove_view_port(s->gui, s->view);
    view_port_free(s->view);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(s->keys);
    furi_record_close(RECORD_STORAGE);
    free(s->tx);
    free(s->tts);
    free(s);
    return 0;
}

static const PmrSpeakPlugin plugin = {.run = speak_app};
static const FlipperAppPluginDescriptor descriptor = {
    .appid = PMR_SPEAK_PLUGIN_ID,
    .ep_api_version = PMR_SPEAK_PLUGIN_API,
    .entry_point = &plugin,
};
const FlipperAppPluginDescriptor* flippie_speak_ep(void) {
    return &descriptor;
}
