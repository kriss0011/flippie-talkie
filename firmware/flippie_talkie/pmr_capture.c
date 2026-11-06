#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include "pmr_ui.h"
#include "pmr_preferences.h"
#include <storage/storage.h>
#include "pmr_cli.h"
#include <stdio.h>
#include <string.h>
#include <stm32wbxx_ll_spi.h>
#include <stm32wbxx_ll_tim.h>
#include "pmr_dsp.h"
#include "pmr_wav.h"
#include "pmr_tx_tone.h"
#include "pmr_radio_core.h"

#define APP_DIR           "/ext/apps_data/pmr_radio"
#define CONFIG_PATH       APP_DIR "/settings.bin"
#define VOICE_DIR         APP_DIR "/voice"
#define SAMPLE_RATE       8000UL
#define CAL_STEP_MS       2800UL
#define BLOCK_SAMPLES     2048U
#define BLOCKS            4U
#define END_BLOCK         UINT32_MAX
#define CAPTURE_DIR       "/ext/apps_data/pmr_voice"
#define WAV_RESERVE_BYTES (4UL * 1024UL * 1024UL)
#define SIGNAL_ON_DBM     (-80)
#define SIGNAL_OFF_DBM    (-85)
#define SIGNAL_TAIL_MS    600U
#define CAL_SETTING_COUNT 16U
#define AUDIO_TARGET_DBM  (-78)

typedef struct {
    uint32_t cycles;
    int8_t rssi;
    uint8_t level, valid, reserved;
} Sample;

typedef struct {
    char magic[8];
    uint32_t header_size, sample_rate, carrier_hz, tuned_hz;
    uint32_t profile, cpu_hz, timestamp, sample_size;
} Header;

typedef struct {
    uint32_t kind, profile, seconds;
    InputEvent input;
} Event;

typedef struct {
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    ViewPort* view;
    uint32_t profile, samples, elapsed, dropped, files;
    bool recording, saving, done, error, receiving, armed, exit_requested;
    int32_t rssi_dbm, peak_dbm;
    uint32_t triggers, read_errors, recoveries, last_signal_tick, marcstate;
    uint32_t tuned_hz, cal_step;
    int32_t selected_setting;
    bool calibration_armed, calibrating;
    bool usb_active, usb_abort;
    bool range_mode;
    int32_t trigger_dbm, release_dbm, audio_floor_dbm, audio_release_dbm;
    bool floor_ready;
    uint32_t audio_setting, carrier_checks, tuning_probes;
    int32_t center_dbm, trigger_center_dbm;
    FuriStreamBuffer* usb_buffer;
    char path[128];
    Storage* storage;
    File* ready_file;
    char ready_path[128];
    bool record_enabled, sd_error, speaker_owned;
    volatile bool sd_writing;
    volatile uint32_t volume;
    uint32_t wav_frames, sd_writes, sd_max_us, gap_frames, clips, saved;
    uint32_t max_irq_cycles, max_gap_cycles;
    uint32_t beep_edges, pcm_edges, speaker_tests;
    uint32_t channel, menu_index;
    uint32_t transport;
    bool tests_open, menu_open, help_open, settings_dirty, settings_error, voice_tx;
    uint32_t settings_changed, notice_until;
    char notice[40];
    bool testing_speaker, transmitting;
    uint32_t tx_result, tx_tests, tx_elapsed_ms, tx_marcstate, tx_tuned_hz;

} App;

typedef struct {
    int32_t offset_hz;
    uint8_t modulation, target, filter, freeze;
} ReceiverSetting;

// Index is stored per sample during calibration. First and last repeat the legacy
// setting, so transmitter movement or level drift can be detected in analysis.
static const ReceiverSetting settings[] = {
    {25000, 0x30, 7, 0, 0},  {25000, 0x00, 3, 0, 0},  {35000, 0x00, 3, 0, 0},
    {45000, 0x00, 3, 0, 0},  {55000, 0x00, 3, 0, 0},  {-25000, 0x00, 3, 0, 0},
    {-35000, 0x00, 3, 0, 0}, {-45000, 0x00, 3, 0, 0}, {-55000, 0x00, 3, 0, 0},
    {35000, 0x30, 4, 1, 0},  {-35000, 0x30, 4, 1, 0}, {35000, 0x00, 3, 1, 0},
    {-35000, 0x00, 3, 1, 0}, {35000, 0x00, 3, 0, 1},  {-35000, 0x00, 3, 0, 1},
    {25000, 0x30, 7, 0, 0},  {-30000, 0x00, 3, 0, 0}, {-20000, 0x00, 3, 0, 0},
    {-32500, 0x00, 3, 0, 0}, {-37500, 0x00, 3, 0, 0}, {-40000, 0x00, 3, 0, 0},
    {-32500, 0x00, 3, 1, 0}, {-30000, 0x00, 3, 1, 0}, {-35000, 0x00, 3, 0, 2},
    {-35000, 0x00, 5, 0, 0}, {-35000, 0x00, 7, 0, 0}, {32500, 0x00, 3, 0, 0},
};
#define SETTING_COUNT (sizeof(settings) / sizeof(settings[0]))
// Revisit the known-good baseline during the sweep to expose level/time drift.
static const uint8_t calibration_plan[CAL_SETTING_COUNT] =
    {6, 16, 18, 19, 20, 6, 12, 21, 22, 23, 14, 6, 24, 25, 26, 6};

// Derived from the firmware's internal 2-FSK receive preset. PA table is zero.
static const uint8_t preset[] = {0x02, 0x0D, 0x0B, 0x06, 0x08, 0x32, 0x07, 0x00, 0x15, 0x04,
                                 0x14, 0x00, 0x13, 0x02, 0x12, 0x00, 0x11, 0x83, 0x10, 0x67,
                                 0x18, 0x18, 0x19, 0x16, 0x1D, 0x90, 0x1C, 0x00, 0x1B, 0x07,
                                 0x20, 0xFB, 0x22, 0x10, 0x21, 0x56, 0,    0,    0,    0,
                                 0,    0,    0,    0,    0,    0};

static void settings_changed(App* app) {
    app->settings_dirty = true;
    app->settings_changed = furi_get_tick();
}
static void settings_load(App* app) {
    PmrPreferences prefs;
    pmr_preferences_load(app->storage, &prefs);
    app->channel = prefs.channel;
    app->volume = prefs.volume;
    app->record_enabled = prefs.record;
    app->transport = prefs.transport;
}
static void settings_save(App* app) {
    if(!app->settings_dirty || app->recording || app->transmitting) return;
    PmrPreferences prefs = {app->channel, app->volume, app->record_enabled, app->transport};
    app->settings_error = !pmr_preferences_save(app->storage, &prefs);
    app->settings_dirty = false;
}
static void notice(App* app, const char* message) {
    snprintf(app->notice, sizeof(app->notice), "%s", message);
    app->notice_until = furi_get_tick() + 3500;
    view_port_update(app->view);
}

static uint32_t cycles_now(void) {
    return furi_hal_cortex_timer_get(0).start;
}

static bool radio_register_locked(uint8_t reg, uint8_t* value, bool read) {
    const FuriHalSpiBusHandle* h = &furi_hal_spi_bus_handle_subghz;
    uint8_t tx[2] = {read ? (reg | 0xC0) : reg, read ? 0 : *value};
    uint8_t rx[2] = {0};
    furi_hal_gpio_write(h->cs, true);
    furi_hal_cortex_delay_us(1);
    furi_hal_gpio_write(h->cs, false);
    FuriHalCortexTimer timeout = furi_hal_cortex_timer_get(1000);
    while(furi_hal_gpio_read(h->miso)) {
        if(furi_hal_cortex_timer_is_expired(timeout)) {
            furi_hal_gpio_write(h->cs, true);
            return false;
        }
    }
    bool ok = furi_hal_spi_bus_trx(h, tx, rx, 2, 5);
    furi_hal_gpio_write(h->cs, true);
    if(read) *value = rx[1];
    return ok && !(rx[0] & 0x80);
}

static bool radio_register(uint8_t reg, uint8_t* value, bool read) {
    furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    bool ok = radio_register_locked(reg, value, read);
    furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
    return ok;
}

static void update_signal(App* app, int8_t raw) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->rssi_dbm = (int32_t)raw / 2 - 74;
    // During capture RSSI is measured off-channel and cannot determine carrier loss.
    if(!app->recording) {
        app->center_dbm = app->rssi_dbm;
        app->receiving = app->rssi_dbm > (app->receiving ? app->release_dbm : app->trigger_dbm);
    }
    if(app->rssi_dbm > app->peak_dbm) app->peak_dbm = app->rssi_dbm;
    if(app->receiving) app->last_signal_tick = furi_get_tick();
    furi_mutex_release(app->mutex);
}

static bool configure_setting_at(App* app, uint32_t index, bool center) {
    if(index >= SETTING_COUNT) return false;
    const ReceiverSetting* cfg = &settings[index];
    furi_hal_subghz_idle();
    furi_hal_subghz_load_custom_preset(preset);
    uint8_t regs[] = {0x10, 0x19, 0x12, 0x1B, 0x1D};
    uint8_t values[] = {0xFA, 0x00, cfg->modulation, cfg->target, (uint8_t)(0x90 | cfg->filter)};
    for(size_t i = 0; i < sizeof(regs); i++) {
        if(!radio_register(regs[i], &values[i], false)) return false;
        uint8_t check = 0;
        if(!radio_register(regs[i], &check, true) || check != values[i]) return false;
    }
    app->tuned_hz = furi_hal_subghz_set_frequency_and_path(
        (uint32_t)((int32_t)pmr_channel_hz(app->channel) + (center ? 0 : cfg->offset_hz)));
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_subghz_rx();
    furi_delay_ms(30);
    if(cfg->freeze && !center) {
        uint8_t value = (cfg->freeze == 2 ? 0x98 : 0x9C) | cfg->filter, check = 0;
        if(!radio_register(0x1D, &value, false) || !radio_register(0x1D, &check, true) ||
           check != value)
            return false;
    }
    return true;
}

static bool configure_setting(App* app, uint32_t index) {
    return configure_setting_at(app, index, false);
}

static bool measure_rssi(int32_t* dbm) {
    int32_t sum = 0;
    for(uint32_t i = 0; i < 8; i++) {
        uint8_t raw = 0;
        if(!radio_register(0x34, &raw, true)) return false;
        sum += (int8_t)raw;
        furi_delay_ms(1);
    }
    *dbm = sum / 16 - 74;
    return true;
}

static bool configure_range(App* app) {
    // Prefer the tone-calibrated steep slope when there is enough level. Move
    // inward only if it is too attenuated; the nearest option is a weak-signal fallback.
    const uint32_t candidates[] = {6, 16, 5, 17}; // -35, -30, -25, -20 kHz
    for(size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if(!configure_setting(app, candidates[i])) return false;
        int32_t dbm;
        if(!measure_rssi(&dbm)) return false;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->audio_setting = candidates[i];
        app->tuning_probes++;
        furi_mutex_release(app->mutex);
        if(dbm >= AUDIO_TARGET_DBM) break;
    }
    return true;
}

static bool configure_radio(App* app, uint32_t profile, bool for_capture) {
    if(!for_capture) return configure_setting_at(app, 6, true);
    if(app->calibrating) return configure_setting(app, calibration_plan[app->cal_step]);
    if(for_capture && app->range_mode && app->selected_setting == 6) return configure_range(app);
    if(for_capture && app->selected_setting >= 0)
        return configure_setting(app, app->selected_setting);
    furi_hal_subghz_idle();
    furi_hal_subghz_load_custom_preset(preset);
    uint8_t value;
    uint32_t requested = pmr_channel_hz(app->channel);
    if(profile != 0) {
        value = 0xFA;
        if(!radio_register(0x10, &value, false)) return false;
        value = (profile == 1) ? 0x16 : 0;
        if(!radio_register(0x19, &value, false)) return false;
    }
    if(profile >= 2) {
        value = 0x30;
        if(!radio_register(0x12, &value, false)) return false;
        if(for_capture)
            requested = profile == 2 ? pmr_channel_hz(app->channel) + 25000 :
                                       pmr_channel_hz(app->channel) - 25000;
    }
    app->tuned_hz = furi_hal_subghz_set_frequency_and_path(requested);
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_subghz_rx();
    furi_delay_ms(30);
    return true;
}

// Called before USB reception is armed. Never learn a noise floor from voice.
static bool prepare_noise_floor(App* app) {
    if(app->recording || app->armed || app->selected_setting < 0) return false;
    int32_t center = -127, audio = -127;
    if(!configure_radio(app, app->profile, false) || !measure_rssi(&center)) return false;
    if(center >= SIGNAL_ON_DBM) return false; // A transmitter may already be on.
    if(!configure_setting(app, app->selected_setting) || !measure_rssi(&audio)) return false;
    bool ok = configure_radio(app, app->profile, false);
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    // Ten dB clearance from the measured floor, capped to avoid false triggers.
    app->trigger_dbm = CLAMP(center + 10, -80, -100);
    app->release_dbm = app->trigger_dbm - 4;
    app->audio_floor_dbm = audio;
    app->audio_release_dbm = CLAMP(audio + 4, -85, -105);
    app->floor_ready = ok;
    furi_mutex_release(app->mutex);
    return ok;
}

static void draw(Canvas* canvas, void* context) {
    App* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    char line[64];
    if(app->transmitting) {
        pmr_ui_radio(
            canvas,
            "Transmit",
            "TX",
            app->channel,
            app->rssi_dbm,
            "Sending",
            app->voice_tx ? "Voice test" : "Tone test");
        elements_button_center(canvas, "Stop");
    } else if(app->help_open) {
        pmr_ui_header(canvas, "Controls", "HELP");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 3, 25, "Up / Down: speaker volume");
        canvas_draw_str(canvas, 3, 36, "Hold OK: send voice test");
        canvas_draw_str(canvas, 3, 47, "WAVs: pmr_voice on SD");
        elements_button_center(canvas, "Back");
    } else if(app->menu_open || app->tests_open) {
        uint32_t count = app->tests_open ? 3 : 5;
        char badge[8];
        snprintf(badge, sizeof(badge), "CH %02lu", app->channel);
        pmr_ui_header(
            canvas,
            app->tests_open ? "Tests" : "Listen config",
            app->tests_open ? badge : (app->settings_error ? "SD error" : ""));
        uint32_t first = app->menu_index >= 4 ? app->menu_index - 3 : 0;
        for(uint32_t row = 0; row < 4 && first + row < count; row++) {
            uint32_t item = first + row;
            const char* label;
            line[0] = 0;
            if(app->tests_open)
                label = item == 0 ? "Send voice test" :
                        item == 1 ? "Send tone test" :
                                    "Test speaker";
            else {
                label = item == 0 ? "Channel" :
                        item == 1 ? "Speaker" :
                        item == 2 ? "Auto-record" :
                        item == 3 ? "Listening" :
                                    "Controls";
                if(item == 0)
                    snprintf(line, sizeof(line), "< %02lu >", app->channel);
                else if(item == 1)
                    snprintf(line, sizeof(line), "< %lu/8 >", app->volume);
                else if(item == 2)
                    snprintf(line, sizeof(line), "< %s >", app->record_enabled ? "On" : "Off");
                else if(item == 3)
                    snprintf(line, sizeof(line), "< %s >", app->armed ? "On" : "Off");
            }
            pmr_ui_row(canvas, row, item == app->menu_index, count > 4, label, line);
        }
        pmr_ui_scroll(canvas, app->menu_index, count);
    } else {
        const char* state = app->saving    ? "Saving" :
                            app->recording ? "Receiving" :
                            app->armed     ? "Listening" :
                                             "Paused";
        snprintf(
            line, sizeof(line), "Vol %lu  Rec %s", app->volume, app->record_enabled ? "on" : "off");
        if(app->recording)
            snprintf(
                line,
                sizeof(line),
                "%lu s  %s",
                app->elapsed,
                app->record_enabled ? "SD WAV" : "Speaker");
        if(app->testing_speaker) snprintf(line, sizeof(line), "Speaker test");
        if(app->error) {
            state = "RX error";
            snprintf(line, sizeof(line), "OK to retry");
        } else if(app->sd_error || app->settings_error)
            snprintf(line, sizeof(line), "SD error");
        else if((int32_t)(app->notice_until - furi_get_tick()) > 0)
            snprintf(line, sizeof(line), "%.18s", app->notice);
        pmr_ui_radio(canvas, "Listen", "Speaker", app->channel, app->rssi_dbm, state, line);
        if(!app->recording && !app->saving) {
            elements_button_left(canvas, "Config");
            elements_button_right(canvas, "Tests");
        }
        elements_button_center(canvas, app->armed ? "Pause" : "Listen");
    }
    furi_mutex_release(app->mutex);
}

// Prepare FAT allocation and the sector-aligned header while the radio is idle.
// The next transmission never waits for file creation or initial allocation.
static void wav_discard_ready(App* app) {
    if(!app->ready_file) return;
    storage_file_close(app->ready_file);
    storage_file_free(app->ready_file);
    app->ready_file = NULL;
    storage_common_remove(app->storage, app->ready_path);
    app->ready_path[0] = 0;
}

static bool wav_prepare(App* app) {
    if(app->ready_file || !app->record_enabled) return true;
    storage_common_mkdir(app->storage, CAPTURE_DIR);
    File* file = storage_file_alloc(app->storage);
    snprintf(
        app->ready_path,
        sizeof(app->ready_path),
        CAPTURE_DIR "/ch%02lu_%lu_%lu.wav",
        app->channel,
        furi_hal_rtc_get_timestamp(),
        furi_get_tick());
    bool created = storage_file_open(file, app->ready_path, FSAM_READ_WRITE, FSOM_CREATE_NEW);
    bool ok = created && storage_file_expand(file, WAV_RESERVE_BYTES);
    uint8_t header[512];
    pmr_wav_header(header, 0);
    if(ok)
        ok = storage_file_seek(file, 0, true) && storage_file_write(file, header, 512) == 512 &&
             storage_file_sync(file);
    if(ok) {
        app->ready_file = file;
        return true;
    }
    storage_file_close(file);
    storage_file_free(file);
    if(created) storage_common_remove(app->storage, app->ready_path);
    app->ready_path[0] = 0;
    app->sd_error = true;
    app->record_enabled = false;
    return false;
}

static void set_recording_option(App* app, bool enabled) {
    if(app->recording) return;
    app->record_enabled = enabled;
    app->sd_error = false;
    if(enabled)
        wav_prepare(app);
    else
        wav_discard_ready(app);
    settings_changed(app);
    view_port_update(app->view);
}

static void volume_key(App* app, InputKey key) {
    uint32_t before = app->volume;
    if(key == InputKeyUp && app->volume < 8) app->volume++;
    if(key == InputKeyDown && app->volume > 0) app->volume--;
    if(before != app->volume) settings_changed(app);
}

static void speaker_pwm_configure(App* app) {
    if(!app->speaker_owned) return;
    // Re-establish all timer state for every reception, including after a beep.
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
}

static void speaker_init(App* app) {
    app->speaker_owned = furi_hal_speaker_acquire(0);
    speaker_pwm_configure(app);
}

static uint32_t speaker_pin_edges(void) {
    uint32_t start = cycles_now(), edges = 0;
    bool previous = furi_hal_gpio_read(&gpio_speaker);
    while((uint32_t)(cycles_now() - start) < 640000) {
        bool now = furi_hal_gpio_read(&gpio_speaker);
        if(now != previous) edges++;
        previous = now;
    }
    return edges;
}

static void speaker_test_irq(void* context) {
    if(!LL_TIM_IsActiveFlag_UPDATE(TIM2)) return;
    LL_TIM_ClearFlag_UPDATE(TIM2);
    uint32_t* phase = context;
    // Known 1 kHz sine at 8 kHz, near full PWM range. No radio involvement.
    static const uint16_t duty[8] = {512, 851, 992, 851, 512, 173, 32, 173};
    TIM16->CCR1 = duty[(*phase)++ & 7];
}

static void speaker_test(App* app) {
    if(app->recording || app->usb_active || !app->speaker_owned) return;
    bool armed = app->armed;
    app->armed = false;
    app->testing_speaker = true;
    view_port_update(app->view);
    furi_hal_speaker_start(1000.0f, 0.8f);
    app->beep_edges = speaker_pin_edges();
    furi_delay_ms(490);
    furi_hal_speaker_stop();
    furi_delay_ms(650);
    speaker_pwm_configure(app);
    furi_hal_bus_enable(FuriHalBusTIM2);
    LL_TIM_InitTypeDef init = {0};
    init.Prescaler = 63;
    init.Autoreload = 124;
    LL_TIM_Init(TIM2, &init);
    uint32_t phase = 0;
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, speaker_test_irq, &phase);
    LL_TIM_ClearFlag_UPDATE(TIM2);
    LL_TIM_EnableIT_UPDATE(TIM2);
    LL_TIM_EnableAllOutputs(TIM16);
    LL_TIM_EnableCounter(TIM2);
    app->pcm_edges = speaker_pin_edges();
    furi_delay_ms(990);
    LL_TIM_DisableIT_UPDATE(TIM2);
    LL_TIM_DisableCounter(TIM2);
    LL_TIM_ClearFlag_UPDATE(TIM2);
    LL_TIM_DisableAllOutputs(TIM16);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, NULL, NULL);
    furi_hal_bus_disable(FuriHalBusTIM2);
    app->speaker_tests++;
    app->testing_speaker = false;
    app->armed = armed;
    view_port_update(app->view);
}

static LevelDuration tx_tone_yield(void* context) {
    bool level = false;
    uint32_t duration = 0;
    if(!pmr_tx_tone_next(context, &level, &duration)) return level_duration_reset();
    return level_duration_make(level, duration);
}

static LevelDuration tx_voice_yield(void* context) {
    bool level;
    uint32_t duration;
    PmrPdm* pdm = context;
    if(!pmr_tx_lead_pulse(&pdm->lead_remaining, &level, &duration) &&
       !pmr_pdm_next(pdm, &level, &duration))
        return level_duration_reset();
    // API 88 HAL converts microseconds to ARR itself; do not subtract twice.
    return level_duration_make(level, duration);
}
static uint8_t* voice_load(App* app, PmrPdm* pdm) {
    char path[96];
    snprintf(path, sizeof(path), VOICE_DIR "/ch%02lu.pdm", app->channel);
    File* file = storage_file_alloc(app->storage);
    uint8_t* data = NULL;
    uint8_t magic[8];
    uint32_t header[4] = {0};
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_read(file, magic, 8) == 8 && !memcmp(magic, "PMRPDM1", 8) &&
              storage_file_read(file, header, 16) == 16;
    uint32_t bytes = (header[0] + 7) / 8;
    ok = ok && header[0] >= 4800 && header[0] <= 48000 * 8 && header[0] % 6 == 0 &&
         header[1] == 48000 && header[2] == app->channel && storage_file_size(file) == 24 + bytes;
    if(ok) {
        data = malloc(bytes);
        ok = storage_file_read(file, data, bytes) == bytes &&
             pmr_checksum(data, bytes) == header[3];
    }
    storage_file_close(file);
    storage_file_free(file);
    if(!ok) {
        free(data);
        return NULL;
    }
    *pdm = (PmrPdm){.bits = data, .count = header[0], .lead_remaining = PMR_TX_LEAD_BITS};
    return data;
}

// Explicit, bounded radio test only. No automatic transmissions on reception.
// Status: 1 complete, 2 firmware refused, 3 channel busy, 4 radio error,
// 5 cancelled, 6 watchdog timeout. Always restore the receiver afterward.
static void radio_test(App* app, bool voice) {
    if(app->recording || app->usb_active || app->testing_speaker) return;
    app->menu_open = false;
    app->help_open = false;
    app->voice_tx = voice;
    app->tx_elapsed_ms = 0;
    app->tx_marcstate = 0;
    app->tx_result = 0;
    if(!furi_hal_subghz_is_tx_allowed(pmr_channel_hz(app->channel))) {
        app->tx_result = 2;
        notice(app, "TX blocked");
        return;
    }
    bool armed = app->armed;
    app->armed = false;
    int32_t center = -127;
    if(!configure_radio(app, app->profile, false) || !measure_rssi(&center)) {
        app->tx_result = 4;
        app->armed = armed;
        notice(app, "Radio error");
        return;
    }
    if(center > app->trigger_dbm) {
        app->tx_result = 3;
        app->armed = armed;
        notice(app, "Channel busy");
        return;
    }
    PmrPdm pdm = {0};
    uint8_t* voice_data = voice ? voice_load(app, &pdm) : NULL;
    if(voice && !voice_data) {
        app->tx_result = 7;
        app->armed = armed;
        notice(app, "No voice file");
        return;
    }
    if(app->speaker_owned) LL_TIM_DisableAllOutputs(TIM16);
    app->transmitting = true;
    view_port_update(app->view);
    furi_hal_subghz_idle();
    furi_hal_subghz_load_custom_preset(preset);
    // Gaussian-shaped FSK, asynchronous input, +/- 2.38 kHz deviation.
    const uint8_t regs[] = {0x12, 0x15, 0x22, 0x10, 0x11};
    // Voice uses 48 kbit/s 2-FSK PDM; tones retain the confirmed GFSK preset.
    uint8_t values[] = {voice ? 0x00 : 0x10, 0x04, 0x10, voice ? 0x6A : 0x67, voice ? 0xE4 : 0x83};
    bool configured = true;
    for(uint32_t i = 0; i < sizeof(regs); i++) {
        uint8_t check = 0;
        if(!radio_register(regs[i], &values[i], false) || !radio_register(regs[i], &check, true) ||
           check != values[i])
            configured = false;
    }
    // TI's nominal 0 dBm 433 MHz table entry; actual radiated power is unmeasured.
    const uint8_t pa[8] = {0x60, 0, 0, 0, 0, 0, 0, 0};
    furi_hal_subghz_load_patable(pa);
    app->tx_tuned_hz = furi_hal_subghz_set_frequency_and_path(pmr_channel_hz(app->channel));
    furi_hal_subghz_set_async_mirror_pin(NULL);
    PmrTxTone tone = {0};
    bool started = configured &&
                   furi_hal_subghz_start_async_tx(
                       voice ? tx_voice_yield : tx_tone_yield, voice ? (void*)&pdm : (void*)&tone);
    if(started) {
        app->tx_tests++;
        // STX first enters calibration; wait for a stable TX state before reporting.
        furi_delay_ms(10);
        uint8_t marc = 0;
        if(radio_register(0x35, &marc, true)) app->tx_marcstate = marc & 0x1F;
        uint32_t start = furi_get_tick();
        bool cancelled = false;
        while(!furi_hal_subghz_is_async_tx_complete() && furi_get_tick() - start < 10000 &&
              !cancelled) {
            Event event;
            if(furi_message_queue_get(app->queue, &event, 20) == FuriStatusOk) {
                bool back = event.kind == 0 && event.input.type == InputTypeShort &&
                            event.input.key == InputKeyBack;
                bool stop = event.kind == 2 ||
                            (event.kind == 0 && event.input.type == InputTypeShort &&
                             event.input.key == InputKeyOk);
                if(back || stop) cancelled = true;
            }
        }
        app->tx_elapsed_ms = furi_get_tick() - start;
        app->tx_result = cancelled ? 5 : furi_hal_subghz_is_async_tx_complete() ? 1 : 6;
        furi_hal_subghz_stop_async_tx();
    } else
        app->tx_result = configured ? 2 : 4;
    free(voice_data);
    furi_hal_subghz_idle();
    bool restored = configure_radio(app, app->profile, false);
    app->transmitting = false;
    app->error = !restored;
    app->armed = armed && restored;
    notice(
        app,
        app->tx_result == 1 ? "Test finished" :
        app->tx_result == 5 ? "Test stopped" :
                              "Test failed");
    view_port_update(app->view);
}

static void change_channel(App* app, uint32_t channel) {
    if(channel < 1 || channel > 16 || app->recording || app->transmitting || app->usb_active)
        return;
    bool armed = app->armed;
    app->armed = false;
    wav_discard_ready(app);
    app->channel = channel;
    app->trigger_dbm = SIGNAL_ON_DBM;
    app->release_dbm = SIGNAL_OFF_DBM;
    app->floor_ready = false;
    app->receiving = false;
    app->notice_until = 0;
    app->rssi_dbm = -127;
    app->peak_dbm = -127;
    app->done = false;
    app->error = !configure_radio(app, app->profile, false);
    if(!app->error) prepare_noise_floor(app);
    if(app->record_enabled) wav_prepare(app);
    app->armed = armed && !app->error;
    settings_changed(app);
    view_port_update(app->view);
}

static void input(InputEvent* input_event, void* context) {
    App* app = context;
    Event event = {.kind = 0, .input = *input_event};
    furi_message_queue_put(app->queue, &event, 0);
}

typedef struct {
    uint32_t index, count;
} WriteBlock;
typedef struct {
    App* app;
    Sample* blocks[BLOCKS];
    FuriMessageQueue* available;
    FuriMessageQueue* pending;
    Header header;
    bool usb;
    File* wav;
    int16_t* pcm[BLOCKS];
} Pipeline;

typedef struct {
    Pipeline* pipe;
    volatile uint32_t index, count, total, dropped, read_errors;
    volatile int32_t sum;
    volatile uint32_t mean_count;
    volatile int8_t last_rssi;
    volatile bool valid, fault;
    uint8_t tag;
    PmrDsp dsp;
    uint32_t previous_stamp, max_gap, max_irq;
} Sampler;

// The capture task owns the SPI bus throughout sampling. This interrupt only
// performs a bounded two-byte transaction, with no mutex, allocation or wait.
// A 32 us deadline prevents a faulty radio from trapping the interrupt handler.
static bool sample_rssi_irq(uint8_t* rssi) {
    const FuriHalSpiBusHandle* h = &furi_hal_spi_bus_handle_subghz;
    SPI_TypeDef* spi = h->bus->spi;
    uint32_t start = cycles_now();
    uint32_t limit = 32U * 64U;
    uint8_t reply[2] = {0};
    bool ok = false;
    furi_hal_gpio_write(h->cs, false);
    while(furi_hal_gpio_read(h->miso)) {
        if((uint32_t)(cycles_now() - start) > limit) goto end;
    }
    for(uint32_t i = 0; i < 2; i++) {
        while(!LL_SPI_IsActiveFlag_TXE(spi)) {
            if((uint32_t)(cycles_now() - start) > limit) goto end;
        }
        LL_SPI_TransmitData8(spi, i == 0 ? 0xF4 : 0);
        while(!LL_SPI_IsActiveFlag_RXNE(spi)) {
            if((uint32_t)(cycles_now() - start) > limit) goto end;
        }
        reply[i] = LL_SPI_ReceiveData8(spi);
    }
    while(LL_SPI_IsActiveFlag_BSY(spi)) {
        if((uint32_t)(cycles_now() - start) > limit) goto end;
    }
    *rssi = reply[1];
    ok = !(reply[0] & 0x80);
end:
    furi_hal_gpio_write(h->cs, true);
    return ok;
}

static void sample_timer_irq(void* context) {
    if(!LL_TIM_IsActiveFlag_UPDATE(TIM2)) return;
    LL_TIM_ClearFlag_UPDATE(TIM2);
    Sampler* sampler = context;
    uint32_t before = cycles_now();
    uint8_t raw = 0;
    bool valid = sample_rssi_irq(&raw);
    uint32_t stamp = before + (uint32_t)(cycles_now() - before) / 2;
    App* app = sampler->pipe->app;
    uint32_t gap = sampler->previous_stamp ? (uint32_t)(stamp - sampler->previous_stamp) : 8000;
    if(gap > sampler->max_gap) sampler->max_gap = gap;
    if(gap > 192000) pmr_dsp_reset(&sampler->dsp);
    sampler->previous_stamp = stamp;
    int16_t pcm = valid ? pmr_dsp_sample(&sampler->dsp, (float)(int8_t)raw * 0.5f) : 0;
    if(!valid) pmr_dsp_reset(&sampler->dsp);
    if(app->speaker_owned) {
        if(app->volume) {
            LL_TIM_OC_SetCompareCH1(TIM16, pmr_speaker_duty(pcm, app->volume));
            LL_TIM_EnableAllOutputs(TIM16);
        } else
            LL_TIM_DisableAllOutputs(TIM16);
    }
    uint32_t irq_cycles = (uint32_t)(cycles_now() - before);
    if(irq_cycles > sampler->max_irq) sampler->max_irq = irq_cycles;
    sampler->last_rssi = (int8_t)raw;
    sampler->valid = valid;
    if(valid) {
        sampler->sum += (int8_t)raw;
        sampler->mean_count++;
    } else
        sampler->read_errors++;
    uint32_t index = sampler->index;
    if(index == END_BLOCK &&
       furi_message_queue_get(sampler->pipe->available, &index, 0) == FuriStatusOk) {
        sampler->index = index;
        sampler->count = 0;
    }
    if(index == END_BLOCK) {
        sampler->dropped++;
        return;
    }
    if(sampler->pipe->pcm[index]) sampler->pipe->pcm[index][sampler->count] = pcm;
    sampler->pipe->blocks[index][sampler->count++] = (Sample){
        .cycles = stamp,
        .rssi = (int8_t)raw,
        .level = furi_hal_gpio_read(&gpio_cc1101_g0),
        .valid = valid,
        .reserved = (uint8_t)(sampler->tag | (app->sd_writing ? 0x80 : 0))};
    sampler->total++;
    if(sampler->count == BLOCK_SAMPLES) {
        WriteBlock block = {.index = index, .count = BLOCK_SAMPLES};
        if(furi_message_queue_put(sampler->pipe->pending, &block, 0) != FuriStatusOk)
            sampler->fault = true;
        sampler->index = END_BLOCK;
        sampler->count = 0;
    }
}

static void sample_timer_pause(void) {
    LL_TIM_DisableIT_UPDATE(TIM2);
    LL_TIM_DisableCounter(TIM2);
    LL_TIM_ClearFlag_UPDATE(TIM2);
}

static void sample_timer_resume(Sampler* sampler, uint8_t tag) {
    pmr_dsp_reset(&sampler->dsp);
    sampler->tag = tag;
    sampler->sum = 0;
    sampler->mean_count = 0;
    LL_TIM_SetCounter(TIM2, 0);
    LL_TIM_ClearFlag_UPDATE(TIM2);
    LL_TIM_EnableIT_UPDATE(TIM2);
    LL_TIM_EnableCounter(TIM2);
}

static void sample_timer_init(Sampler* sampler) {
    furi_hal_bus_enable(FuriHalBusTIM2);
    LL_TIM_InitTypeDef init = {0};
    init.Prescaler = 64 - 1;
    init.CounterMode = LL_TIM_COUNTERMODE_UP;
    init.Autoreload = 1000000UL / SAMPLE_RATE - 1;
    init.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    LL_TIM_Init(TIM2, &init);
    LL_TIM_SetClockSource(TIM2, LL_TIM_CLOCKSOURCE_INTERNAL);
    LL_TIM_DisableARRPreload(TIM2);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, sample_timer_irq, sampler);
}

static bool io_failed(App* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool error = app->error;
    furi_mutex_release(app->mutex);
    return error;
}

// Only the CLI callback touches the USB session. One writer and one reader use
// this bounded bridge; the receiver never waits for a filesystem write in USB mode.
static bool usb_send(App* app, const void* data, size_t length) {
    const uint8_t* bytes = data;
    while(length) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool abort = app->usb_abort;
        furi_mutex_release(app->mutex);
        if(abort) return false;
        size_t n = furi_stream_buffer_send(app->usb_buffer, bytes, length, 50);
        bytes += n;
        length -= n;
    }
    return true;
}

static bool usb_frame(App* app, uint32_t kind, const void* data, uint32_t size) {
    uint32_t frame[] = {0x31524D50UL, kind, size};
    return usb_send(app, frame, sizeof(frame)) && usb_send(app, data, size);
}

typedef struct {
    Pipeline* pipe;
    int16_t data[2048];
    uint32_t count, frames, previous_stamp;
    bool ok;
} WavWriter;

static void wav_flush(WavWriter* w) {
    if(!w->count || !w->ok) return;
    App* app = w->pipe->app;
    app->sd_writing = true;
    uint32_t start = cycles_now();
    uint32_t bytes = w->count * sizeof(int16_t);
    w->ok = storage_file_write(w->pipe->wav, w->data, bytes) == bytes;
    uint32_t us = (uint32_t)(cycles_now() - start) / 64;
    app->sd_writing = false;
    app->sd_writes++;
    if(us > app->sd_max_us) app->sd_max_us = us;
    if(w->ok) {
        w->frames += w->count;
        app->wav_frames = w->frames;
    } else
        app->sd_error = true;
    w->count = 0;
}

static void wav_sample(WavWriter* w, int16_t sample) {
    if(!w->ok) return;
    // Keep RIFF sizes within 32 bits. Close with an explicit error at the format limit.
    if(w->frames >= 0x7FFFF000UL) {
        w->ok = false;
        w->pipe->app->sd_error = true;
        return;
    }
    w->data[w->count++] = sample;
    if(w->count == 2048) wav_flush(w);
}

static int32_t write_worker(void* context) {
    Pipeline* pipe = context;
    App* app = pipe->app;
    bool ok = !pipe->usb || usb_frame(app, 1, &pipe->header, sizeof(Header));
    WavWriter* wav = pipe->wav ? malloc(sizeof(WavWriter)) : NULL;
    if(wav) {
        *wav = (WavWriter){.pipe = pipe, .ok = true};
    }
    WriteBlock block;
    for(;;) {
        if(!ok) app->error = true;
        furi_message_queue_get(pipe->pending, &block, FuriWaitForever);
        if(block.index == END_BLOCK) break;
        if(ok && pipe->usb)
            ok = usb_frame(app, 2, pipe->blocks[block.index], block.count * sizeof(Sample));
        if(wav && wav->ok) {
            for(uint32_t i = 0; i < block.count; i++) {
                const Sample* sample = &pipe->blocks[block.index][i];
                if(wav->previous_stamp) {
                    uint32_t ticks = (uint32_t)(sample->cycles - wav->previous_stamp);
                    uint32_t missing = ticks > 12000 ? (ticks + 4000) / 8000 - 1 : 0;
                    // Preserve pauses/lost samples on the WAV timeline, including centre checks.
                    for(uint32_t j = 0; j < missing; j++)
                        wav_sample(wav, 0);
                    app->gap_frames += missing;
                }
                wav->previous_stamp = sample->cycles;
                wav_sample(wav, pipe->pcm[block.index][i]);
            }
        }
        furi_message_queue_put(pipe->available, &block.index, FuriWaitForever);
    }
    if(wav) {
        wav_flush(wav);
        uint8_t header[512];
        pmr_wav_header(header, wav->frames);
        bool finalized = storage_file_seek(pipe->wav, 512 + wav->frames * 2, true) &&
                         storage_file_truncate(pipe->wav);
        finalized = finalized && storage_file_seek(pipe->wav, 0, true) &&
                    storage_file_write(pipe->wav, header, 512) == 512;
        finalized = finalized && storage_file_sync(pipe->wav);
        storage_file_close(pipe->wav);
        storage_file_free(pipe->wav);
        if(!finalized || !wav->ok)
            app->sd_error = true;
        else
            app->saved++;
        free(wav);
    }
    if(pipe->usb) {
        uint32_t result[] = {app->samples, app->dropped, app->error};
        int32_t diagnostic[] = {
            app->trigger_center_dbm,
            app->center_dbm,
            (int32_t)app->audio_setting,
            (int32_t)app->carrier_checks,
            (int32_t)app->tuning_probes,
            app->range_mode};
        int32_t receiver[] = {
            app->trigger_dbm,
            app->release_dbm,
            app->audio_floor_dbm,
            app->audio_release_dbm,
            app->floor_ready,
            17};
        int32_t local[] = {
            app->wav_frames,
            app->sd_writes,
            app->sd_max_us,
            app->gap_frames,
            app->clips,
            app->sd_error};
        if(ok) ok = usb_frame(app, 4, diagnostic, sizeof(diagnostic));
        if(ok) ok = usb_frame(app, 5, receiver, sizeof(receiver));
        if(ok) ok = usb_frame(app, 6, local, sizeof(local));
        if(ok) ok = usb_frame(app, 3, result, sizeof(result));
    }
    if(!ok) app->error = true;
    return ok ? 0 : 1;
}

// limit_seconds is only used by the USB bench test. Automatic captures have no timer limit.
static bool capture(App* app, uint32_t limit_seconds) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->usb_active && app->usb_abort) {
        furi_mutex_release(app->mutex);
        return false;
    }
    app->recording = true;
    app->trigger_center_dbm = app->center_dbm;
    app->carrier_checks = 0;
    app->tuning_probes = 0;
    app->audio_setting = app->selected_setting >= 0 ? (uint32_t)app->selected_setting : 0;
    app->calibrating = app->calibration_armed;
    app->calibration_armed = false;
    app->cal_step = 0;
    bool calibration = app->calibrating;
    bool adaptive = app->range_mode && app->selected_setting == 6 && !calibration;
    furi_mutex_release(app->mutex);
    // Monitor at the real channel centre. Apply experimental detuning only after triggering.
    if(!configure_radio(app, app->profile, true)) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->error = true;
        app->armed = false;
        app->calibrating = false;
        app->recording = false;
        furi_mutex_release(app->mutex);
        return false;
    }
    Pipeline pipe = {.app = app, .usb = app->usb_active};
    if(app->record_enabled && !calibration) {
        pipe.wav = app->ready_file;
        app->ready_file = NULL;
        if(!pipe.wav) {
            app->sd_error = true;
            app->record_enabled = false;
        } else {
            snprintf(app->path, sizeof(app->path), "%s", app->ready_path);
            app->ready_path[0] = 0;
        }
    }
    app->wav_frames = 0;
    app->sd_writes = 0;
    app->sd_max_us = 0;
    app->gap_frames = 0;
    app->clips = 0;
    pipe.available = furi_message_queue_alloc(BLOCKS, sizeof(uint32_t));
    pipe.pending = furi_message_queue_alloc(BLOCKS + 1, sizeof(WriteBlock));
    for(uint32_t i = 0; i < BLOCKS; i++) {
        pipe.blocks[i] = malloc(sizeof(Sample) * BLOCK_SAMPLES);
        if(pipe.wav) pipe.pcm[i] = malloc(sizeof(int16_t) * BLOCK_SAMPLES);
        furi_message_queue_put(pipe.available, &i, FuriWaitForever);
    }
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->samples = 0;
    app->elapsed = 0;
    app->dropped = 0;
    app->recording = true;
    app->saving = false;
    app->done = false;
    app->error = false;
    if(!pipe.wav)
        snprintf(app->path, sizeof(app->path), pipe.usb ? "USB_to_computer" : "Live_speaker");
    furi_mutex_release(app->mutex);
    uint32_t cpu_hz = furi_hal_cortex_instructions_per_microsecond() * 1000000UL;
    pipe.header = (Header){
        .magic = "PMRCAP3",
        .header_size = sizeof(Header),
        .sample_rate = SAMPLE_RATE,
        .carrier_hz = pmr_channel_hz(app->channel),
        .tuned_hz = app->tuned_hz,
        .profile = calibration                ? 110 :
                   adaptive                   ? 300 :
                   app->selected_setting >= 0 ? (uint32_t)(200 + app->selected_setting) :
                                                app->profile,
        .cpu_hz = cpu_hz,
        .timestamp = furi_hal_rtc_get_timestamp(),
        .sample_size = sizeof(Sample)};
    FuriThread* writer = furi_thread_alloc_ex("PmrSave", 3072, write_worker, &pipe);
    furi_thread_start(writer);
    view_port_update(app->view);
    uint32_t start = furi_get_tick(), last_signal = start;
    uint32_t total = 0, dropped = 0, next_ui = start;
    Sampler sampler = {.pipe = &pipe, .index = END_BLOCK};
    bool stop = false;
    uint32_t step_start = start, mean_start = start;
    uint32_t check_wait = SIGNAL_TAIL_MS;
    speaker_pwm_configure(app);
    furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    sample_timer_init(&sampler);
    sample_timer_resume(
        &sampler,
        calibration ? 1 :
        adaptive    ? (uint8_t)(app->audio_setting + 1) :
                      0);
    while(!stop) {
        furi_delay_ms(10);
        uint32_t tick = furi_get_tick();
        total = sampler.total;
        dropped = sampler.dropped;
        bool valid = sampler.valid;
        int8_t rssi = sampler.last_rssi;
        if(tick - mean_start >= 20) {
            FURI_CRITICAL_ENTER();
            int32_t sum = sampler.sum;
            uint32_t count = sampler.mean_count;
            sampler.sum = 0;
            sampler.mean_count = 0;
            FURI_CRITICAL_EXIT();
            if(count && sum / (int32_t)(2 * count) - 74 > app->audio_release_dbm) {
                last_signal = tick;
                check_wait = SIGNAL_TAIL_MS;
            }
            mean_start = tick;
        }
        if(sampler.fault || sampler.read_errors > 8) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->error = true;
            app->armed = false;
            furi_mutex_release(app->mutex);
            stop = true;
        }
        if((int32_t)(tick - next_ui) >= 0) {
            if(valid) update_signal(app, (int8_t)rssi);
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->samples = total;
            app->elapsed = (tick - start) / 1000;
            app->dropped = dropped;
            furi_mutex_release(app->mutex);
            view_port_update(app->view);
            next_ui = tick + 200;
            if(io_failed(app)) stop = true;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(pipe.usb && app->usb_abort) stop = true;
            furi_mutex_release(app->mutex);
            Event event;
            while(furi_message_queue_get(app->queue, &event, 0) == FuriStatusOk) {
                bool back = event.kind == 0 && event.input.type == InputTypeShort &&
                            event.input.key == InputKeyBack;
                bool pause = event.kind == 2 ||
                             (event.kind == 0 && event.input.type == InputTypeShort &&
                              event.input.key == InputKeyOk);
                if(event.kind == 0 &&
                   (event.input.type == InputTypeShort || event.input.type == InputTypeRepeat))
                    volume_key(app, event.input.key);
                if(back || pause) {
                    stop = true;
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->armed = false;
                    if(back) app->exit_requested = true;
                    furi_mutex_release(app->mutex);
                }
            }
        }
        if(calibration && tick - step_start >= CAL_STEP_MS) {
            if(app->cal_step + 1 >= CAL_SETTING_COUNT)
                stop = true;
            else if(!stop) {
                sample_timer_pause();
                if(app->speaker_owned) LL_TIM_DisableAllOutputs(TIM16);
                furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->cal_step++;
                furi_mutex_release(app->mutex);
                bool configured = configure_setting(app, calibration_plan[app->cal_step]);
                if(!configured) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->error = true;
                    furi_mutex_release(app->mutex);
                    stop = true;
                }
                furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
                step_start = furi_get_tick();
                if(!stop) sample_timer_resume(&sampler, (uint8_t)(app->cal_step + 1));
            }
        }
        if(!calibration) {
            if(limit_seconds) {
                if(tick - start >= limit_seconds * 1000UL) stop = true;
            } else if(!stop && tick - last_signal >= check_wait) {
                // A weak off-center audio measurement requests a carrier check,
                // never an immediate stop. SPI must be released before retuning.
                sample_timer_pause();
                if(app->speaker_owned) LL_TIM_DisableAllOutputs(TIM16);
                furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
                int32_t center = -127;
                bool checked = configure_radio(app, app->profile, false) && measure_rssi(&center);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->carrier_checks++;
                app->center_dbm = center;
                app->receiving = checked && center > app->release_dbm;
                if(!checked) {
                    app->error = true;
                    app->armed = false;
                }
                bool carrier = app->receiving;
                furi_mutex_release(app->mutex);
                if(!checked || !carrier)
                    stop = true;
                else if(!configure_radio(app, app->profile, true)) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->error = true;
                    app->armed = false;
                    furi_mutex_release(app->mutex);
                    stop = true;
                }
                furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
                // Weak but confirmed carriers need fewer destructive centre checks.
                if(carrier && check_wait < 2400U) check_wait *= 2;
                last_signal = furi_get_tick();
                mean_start = last_signal;
                if(!stop)
                    sample_timer_resume(
                        &sampler, adaptive ? (uint8_t)(app->audio_setting + 1) : 0);
            }
        }
    }
    sample_timer_pause();
    if(app->speaker_owned) LL_TIM_DisableAllOutputs(TIM16);
    app->clips = sampler.dsp.clips;
    app->max_gap_cycles = sampler.max_gap;
    app->max_irq_cycles = sampler.max_irq;
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, NULL, NULL);
    furi_hal_bus_disable(FuriHalBusTIM2);
    total = sampler.total;
    dropped = sampler.dropped;
    furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
    if(sampler.index != END_BLOCK && sampler.count) {
        WriteBlock block = {.index = sampler.index, .count = sampler.count};
        furi_message_queue_put(pipe.pending, &block, FuriWaitForever);
    }
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->samples = total;
    app->dropped = dropped;
    app->saving = true;
    furi_mutex_release(app->mutex);
    view_port_update(app->view);
    WriteBlock end = {.index = END_BLOCK, .count = 0};
    furi_message_queue_put(pipe.pending, &end, FuriWaitForever);
    furi_thread_join(writer);
    furi_thread_free(writer);
    for(uint32_t i = 0; i < BLOCKS; i++) {
        free(pipe.blocks[i]);
        free(pipe.pcm[i]);
    }
    furi_message_queue_free(pipe.available);
    furi_message_queue_free(pipe.pending);
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool ok = !app->error;
    app->recording = false;
    app->saving = false;
    app->done = ok;
    app->calibrating = false;
    if(calibration) app->armed = false;
    if(ok)
        app->files++;
    else
        app->armed = false;
    furi_mutex_release(app->mutex);
    if(app->sd_error) app->record_enabled = false;
    if(!app->exit_requested && app->record_enabled) wav_prepare(app);
    bool monitor_ok = configure_radio(app, app->profile, false);
    if(!monitor_ok) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->error = true;
        app->armed = false;
        furi_mutex_release(app->mutex);
    }
    view_port_update(app->view);
    return ok && monitor_ok;
}

static void cli_usb(PipeSide* cli, App* app, uint32_t test_seconds, bool calibration) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->recording || app->usb_active) {
        furi_mutex_release(app->mutex);
        printf("BUSY\r\n");
        return;
    }
    app->usb_buffer = furi_stream_buffer_alloc(8192, 1);
    app->usb_abort = false;
    app->usb_active = true;
    app->armed = test_seconds == 0;
    app->error = false;
    app->calibration_armed = calibration;
    uint32_t initial_files = app->files;
    furi_mutex_release(app->mutex);
    printf("PMRUSB1\r\n");
    if(test_seconds) {
        Event event = {.kind = 7, .seconds = test_seconds == UINT32_MAX ? 0 : test_seconds};
        furi_message_queue_put(app->queue, &event, FuriWaitForever);
    }
    uint8_t bytes[512];
    uint32_t start = furi_get_tick();
    bool ending = false;
    for(;;) {
        bool connected = (pipe_state(cli) == PipeStateOpen);
        if(!ending && (!connected || cli_is_pipe_broken_or_is_etx_next_char(cli))) {
            ending = true;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->usb_abort = !connected;
            app->armed = false;
            furi_mutex_release(app->mutex);
        }
        size_t n = furi_stream_buffer_receive(app->usb_buffer, bytes, sizeof(bytes), 20);
        if(n && connected) pipe_send(cli, bytes, n);
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool busy = app->recording;
        bool finish_test =
            (test_seconds || calibration) &&
            (app->files > initial_files || (app->error && furi_get_tick() - start > 500));
        bool external_abort = app->usb_abort;
        furi_mutex_release(app->mutex);
        if(!busy && (ending || finish_test || external_abort) &&
           furi_stream_buffer_is_empty(app->usb_buffer))
            break;
    }
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->armed = false;
    app->usb_active = false;
    furi_stream_buffer_free(app->usb_buffer);
    app->usb_buffer = NULL;
    furi_mutex_release(app->mutex);
    printf("\r\nUSB_END\r\n");
}

static void cli_command(PipeSide* cli, FuriString* args, void* context) {
    App* app = context;
    unsigned long profile = 1, seconds = 12;
    const char* text = furi_string_get_cstr(args);
    if(strcmp(text, "tx_tones") == 0 || strcmp(text, "tx_voice") == 0) {
        Event event = {.kind = strcmp(text, "tx_voice") == 0 ? 13 : 12};
        printf(
            "%s\r\n",
            (app->recording || app->usb_active || app->transmitting || app->testing_speaker) ?
                "BUSY" :
            furi_message_queue_put(app->queue, &event, 0) == FuriStatusOk ? "QUEUED" :
                                                                            "QUEUE_FULL");
        return;
    }
    if(sscanf(text, "channel %lu", &profile) == 1) {
        if(profile < 1 || profile > 16) {
            printf("INVALID_CHANNEL\r\n");
            return;
        }
        Event event = {.kind = 14, .profile = profile};
        printf(
            "%s\r\n",
            (app->recording || app->usb_active || app->transmitting)      ? "BUSY" :
            furi_message_queue_put(app->queue, &event, 0) == FuriStatusOk ? "QUEUED" :
                                                                            "QUEUE_FULL");
        return;
    }
    if(strcmp(text, "tx_status") == 0) {
        printf(
            "active=%u result=%lu tests=%lu elapsed_ms=%lu marcstate=%lu tuned_hz=%lu firmware_allowed=%u\r\n",
            app->transmitting,
            app->tx_result,
            app->tx_tests,
            app->tx_elapsed_ms,
            app->tx_marcstate,
            app->tx_tuned_hz,
            furi_hal_subghz_is_tx_allowed(pmr_channel_hz(app->channel)));
        return;
    }
    if(strcmp(text, "speaker_test") == 0) {
        Event event = {.kind = 11};
        printf(
            "%s\r\n",
            (app->recording || app->usb_active)                           ? "BUSY" :
            furi_message_queue_put(app->queue, &event, 0) == FuriStatusOk ? "QUEUED" :
                                                                            "QUEUE_FULL");
        return;
    }
    if(strcmp(text, "speaker_diag") == 0) {
        printf(
            "tests=%lu beep_edges=%lu pcm_edges=%lu cr1=%lu ccer=%lu bdtr=%lu psc=%lu arr=%lu ccr=%lu pin_mode=%lu pin_af=%lu\r\n",
            app->speaker_tests,
            app->beep_edges,
            app->pcm_edges,
            TIM16->CR1,
            TIM16->CCER,
            TIM16->BDTR,
            TIM16->PSC,
            TIM16->ARR,
            TIM16->CCR1,
            (GPIOB->MODER >> 16) & 3,
            (GPIOB->AFR[1]) & 15);
        return;
    }
    if(strcmp(text, "record on") == 0 || strcmp(text, "record off") == 0) {
        if(app->recording) {
            printf("BUSY\r\n");
            return;
        }
        Event event = {.kind = 9, .profile = strcmp(text, "record on") == 0};
        printf(
            "%s\r\n",
            furi_message_queue_put(app->queue, &event, 0) == FuriStatusOk ? "QUEUED" :
                                                                            "QUEUE_FULL");
        return;
    }
    if(sscanf(text, "volume %lu", &profile) == 1 && profile <= 8) {
        app->volume = profile;
        settings_changed(app);
        printf("OK\r\n");
        return;
    }
    if(sscanf(text, "sdtest %lu", &seconds) == 1 && seconds >= 1 && seconds <= 120) {
        Event event = {.kind = 10, .seconds = seconds};
        printf(
            "%s\r\n",
            app->recording                                                ? "BUSY" :
            furi_message_queue_put(app->queue, &event, 0) == FuriStatusOk ? "QUEUED" :
                                                                            "QUEUE_FULL");
        return;
    }
    if(strcmp(text, "usbsweep") == 0) {
        cli_usb(cli, app, 0, true);
        return;
    }
    if(strcmp(text, "usbsweeptest") == 0) {
        cli_usb(cli, app, 1, true);
        return;
    }
    if(strcmp(text, "prepare") == 0) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool busy = app->recording || app->armed || app->usb_active;
        if(!busy) app->floor_ready = false;
        furi_mutex_release(app->mutex);
        Event event = {.kind = 8};
        printf(
            "%s\r\n",
            busy                                                          ? "BUSY" :
            furi_message_queue_put(app->queue, &event, 0) == FuriStatusOk ? "QUEUED" :
                                                                            "QUEUE_FULL");
        return;
    }
    if(strcmp(text, "usbrelease") == 0) {
        cli_usb(cli, app, UINT32_MAX, false);
        return;
    }
    if(strcmp(text, "usb") == 0) {
        cli_usb(cli, app, 0, false);
        return;
    }
    if(sscanf(text, "usbtest %lu", &seconds) == 1 && seconds >= 1 && seconds <= 30) {
        cli_usb(cli, app, seconds, false);
        return;
    }
    if(sscanf(text, "start %lu %lu", &profile, &seconds) == 2 && profile < 4 && seconds >= 1 &&
       seconds <= 120) {
        Event event = {.kind = 1, .profile = profile, .seconds = seconds};
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool busy = app->recording;
        furi_mutex_release(app->mutex);
        if(busy)
            printf("BUSY\r\n");
        else
            printf(
                "%s\r\n",
                furi_message_queue_put(app->queue, &event, 0) == FuriStatusOk ? "QUEUED" :
                                                                                "QUEUE_FULL");
        return;
    }
    if(strcmp(text, "calibrate") == 0 || strcmp(text, "caltest") == 0 ||
       sscanf(text, "select %lu", &profile) == 1) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool busy = app->recording;
        furi_mutex_release(app->mutex);
        if(busy) {
            printf("BUSY\r\n");
            return;
        }
        Event event = {
            .kind = strcmp(text, "calibrate") == 0 ? 4 :
                    strcmp(text, "caltest") == 0   ? 5 :
                                                     6,
            .profile = profile};
        if(event.kind == 6 && profile >= SETTING_COUNT) {
            printf("INVALID_SETTING\r\n");
            return;
        }
        printf(
            "%s\r\n",
            furi_message_queue_put(app->queue, &event, 0) == FuriStatusOk ? "QUEUED" :
                                                                            "QUEUE_FULL");
        return;
    }
    if(strcmp(text, "range on") == 0 || strcmp(text, "range off") == 0) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool busy = app->recording;
        if(!busy) app->range_mode = strcmp(text, "range on") == 0;
        furi_mutex_release(app->mutex);
        printf("%s\r\n", busy ? "BUSY" : "OK");
        return;
    }
    if(strcmp(text, "arm") == 0) {
        Event event = {.kind = 3};
        furi_message_queue_put(app->queue, &event, 0);
    }
    if(strcmp(text, "stop") == 0) {
        Event event = {.kind = 2};
        furi_message_queue_put(app->queue, &event, 0);
    }
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    printf(
        "version=18 on_dbm=%ld off_dbm=%ld audio_floor=%ld audio_off=%ld floor_ready=%u state=%s profile=%lu samples=%lu elapsed=%lu signal=%s rssi=%ld armed=%u dropped=%lu files=%lu peak=%ld legacy_trigger_dbm=-80 triggers=%lu rx_state=%lu read_errors=%lu recoveries=%lu last_signal_tick=%lu tuned_hz=%lu calibration=%s cal_step=%lu selected=%ld range=%u audio_setting=%lu center=%ld carrier_checks=%lu tuning_probes=%lu file=%s\r\n",
        app->trigger_dbm,
        app->release_dbm,
        app->audio_floor_dbm,
        app->audio_release_dbm,
        app->floor_ready,
        app->recording ? "recording" :
        app->error     ? "error" :
        app->done      ? "saved" :
                         "ready",
        app->profile,
        app->samples,
        app->elapsed,
        app->receiving ? "RECEIVING" : "IDLE",
        app->rssi_dbm,
        app->armed,
        app->dropped,
        app->files,
        app->peak_dbm,
        app->triggers,
        app->marcstate,
        app->read_errors,
        app->recoveries,
        app->last_signal_tick,
        app->tuned_hz,
        app->calibrating       ? "running" :
        app->calibration_armed ? "waiting" :
                                 "off",
        app->cal_step,
        app->selected_setting,
        app->range_mode,
        app->audio_setting,
        app->center_dbm,
        app->carrier_checks,
        app->tuning_probes,
        app->path);
    printf(
        "channel=%lu frequency=%lu menu=%u menu_index=%lu settings_error=%u voice=%u\r\n",
        app->channel,
        pmr_channel_hz(app->channel),
        app->menu_open,
        app->menu_index,
        app->settings_error,
        app->voice_tx);
    printf(
        "local_record=%u speaker=%u volume=%lu saved=%lu sd_error=%u wav_frames=%lu sd_writes=%lu sd_max_us=%lu gap_frames=%lu clips=%lu max_irq_cycles=%lu max_gap_cycles=%lu ready_file=%u\r\n",
        app->record_enabled,
        app->speaker_owned,
        app->volume,
        app->saved,
        app->sd_error,
        app->wav_frames,
        app->sd_writes,
        app->sd_max_us,
        app->gap_frames,
        app->clips,
        app->max_irq_cycles,
        app->max_gap_cycles,
        app->ready_file != NULL);
    furi_mutex_release(app->mutex);
}

int32_t pmr_capture_app(void* args) {
    UNUSED(args);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->profile = 2;
    app->volume = 8;
    app->channel = 9;
    app->record_enabled = true;
    app->storage = furi_record_open(RECORD_STORAGE);
    settings_load(app);
    settings_changed(app);
    // RSSI strength is not an audio-quality score. Keep the calibrated offset fixed.
    app->selected_setting = 6;
    app->audio_setting = 6;
    app->range_mode = false;
    app->trigger_dbm = SIGNAL_ON_DBM;
    app->release_dbm = SIGNAL_OFF_DBM;
    app->audio_floor_dbm = -100;
    app->audio_release_dbm = SIGNAL_OFF_DBM;
    app->armed = false;
    app->queue = furi_message_queue_alloc(8, sizeof(Event));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->view = view_port_alloc();
    view_port_draw_callback_set(app->view, draw, app);
    view_port_input_callback_set(app->view, input, app);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app->view, GuiLayerFullscreen);
    CliRegistry* cli = furi_record_open(RECORD_CLI);
    cli_registry_add_command(cli, "pmr_capture", CliCommandFlagParallelSafe, cli_command, app);
    furi_hal_power_insomnia_enter();
    furi_hal_subghz_reset();
    app->rssi_dbm = -127;
    app->peak_dbm = -127;
    app->error = !configure_radio(app, app->profile, false);
    speaker_init(app);
    prepare_noise_floor(app);
    if(app->record_enabled) wav_prepare(app);
    settings_save(app);
    app->armed = !app->error;
    Event event;
    bool running = true;
    uint32_t bad_reads = 0, stable_signal = 0;
    while(running && !app->exit_requested) {
        if(app->settings_dirty && furi_get_tick() - app->settings_changed > 500)
            settings_save(app);
        if(furi_message_queue_get(app->queue, &event, 20) != FuriStatusOk) {
            uint8_t raw = 0, marc = 0;
            bool valid = radio_register(0x34, &raw, true) && radio_register(0x35, &marc, true);
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->marcstate = marc & 0x1F;
            if(!valid) app->read_errors++;
            furi_mutex_release(app->mutex);
            if(valid && (marc & 0x1F) == 0x0D) {
                bad_reads = 0;
                update_signal(app, (int8_t)raw);
                stable_signal = app->receiving ? (stable_signal < 3 ? stable_signal + 1 : 3) : 0;
                if(app->armed && stable_signal >= 3 && !app->error && !app->menu_open &&
                   !app->tests_open && !app->help_open) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->triggers++;
                    furi_mutex_release(app->mutex);
                    capture(app, 0);
                    stable_signal = 0;
                }
            } else if(++bad_reads >= 3) {
                bool recovered = configure_radio(app, app->profile, false);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->recoveries++;
                if(!recovered) {
                    app->error = true;
                    app->armed = false;
                }
                furi_mutex_release(app->mutex);
                bad_reads = 0;
            }
            view_port_update(app->view);
            continue;
        }
        stable_signal = 0;
        if(event.kind == 14) {
            change_channel(app, event.profile);
        } else if(event.kind == 12 || event.kind == 13) {
            radio_test(app, event.kind == 13);
        } else if(event.kind == 11) {
            speaker_test(app);
        } else if(event.kind == 9) {
            set_recording_option(app, event.profile != 0);
        } else if(event.kind == 10) {
            if(!app->usb_active) {
                app->armed = false;
                capture(app, event.seconds);
            }
        } else if(event.kind == 8) {
            prepare_noise_floor(app);
        } else if(event.kind == 7) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool active = app->usb_active && !app->usb_abort;
            furi_mutex_release(app->mutex);
            if(active) capture(app, event.seconds);
        } else if(event.kind == 4 || event.kind == 5) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->calibration_armed = true;
            app->armed = true;
            app->error = false;
            app->cal_step = 0;
            furi_mutex_release(app->mutex);
            if(event.kind == 5) capture(app, 0);
        } else if(event.kind == 6) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->selected_setting = event.profile;
            app->calibration_armed = false;
            app->error = false;
            furi_mutex_release(app->mutex);
        } else if(event.kind == 1) {
            app->profile = event.profile;
            app->selected_setting = -1;
            app->error = !configure_radio(app, app->profile, false);
            if(!app->error) capture(app, event.seconds);
        } else if(event.kind == 2 || event.kind == 3) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->armed = event.kind == 3;
            app->calibration_armed = false;
            if(app->armed) app->error = false;
            furi_mutex_release(app->mutex);
        } else if(
            event.kind == 0 && event.input.type == InputTypeLong &&
            event.input.key == InputKeyOk) {
            if(!app->menu_open && !app->tests_open && !app->help_open) radio_test(app, true);
        } else if(
            event.kind == 0 &&
            (event.input.type == InputTypeShort || event.input.type == InputTypeRepeat)) {
            InputKey key = event.input.key;
            bool short_press = event.input.type == InputTypeShort;
            if(app->help_open) {
                if(short_press && (key == InputKeyBack || key == InputKeyOk))
                    app->help_open = false;
            } else if(app->menu_open || app->tests_open) {
                uint32_t count = app->tests_open ? 3 : 5;
                if(key == InputKeyUp)
                    app->menu_index = (app->menu_index + count - 1) % count;
                else if(key == InputKeyDown)
                    app->menu_index = (app->menu_index + 1) % count;
                else if(key == InputKeyBack && short_press) {
                    app->menu_open = false;
                    app->tests_open = false;
                } else if(app->tests_open && key == InputKeyOk && short_press) {
                    uint32_t item = app->menu_index;
                    app->tests_open = false;
                    if(item < 2)
                        radio_test(app, item == 0);
                    else
                        speaker_test(app);
                } else if(
                    app->menu_open && (key == InputKeyLeft || key == InputKeyRight ||
                                       (key == InputKeyOk && short_press))) {
                    uint32_t item = app->menu_index;
                    bool increase = key != InputKeyLeft;
                    if(item == 0)
                        change_channel(
                            app,
                            increase ? (app->channel % 16 + 1) :
                                       (app->channel == 1 ? 16 : app->channel - 1));
                    else if(item == 1)
                        volume_key(app, increase ? InputKeyUp : InputKeyDown);
                    else if(item == 2)
                        set_recording_option(app, !app->record_enabled);
                    else if(item == 3) {
                        app->armed = !app->armed;
                        if(app->armed) app->error = !configure_radio(app, app->profile, false);
                    } else if(key == InputKeyOk && short_press)
                        app->help_open = true;
                }
            } else {
                volume_key(app, key);
                if(key == InputKeyBack && short_press)
                    running = false;
                else if(key == InputKeyOk && short_press) {
                    app->armed = !app->armed;
                    if(app->armed) app->error = !configure_radio(app, app->profile, false);
                } else if(key == InputKeyLeft && short_press) {
                    app->menu_open = true;
                    app->menu_index = 0;
                } else if(key == InputKeyRight && short_press) {
                    app->tests_open = true;
                    app->menu_index = 0;
                }
            }
            view_port_update(app->view);
        }
    }
    // Keep App and the bridge alive until the CLI session has stopped reading.
    for(;;) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool active = app->usb_active;
        app->usb_abort = true;
        app->armed = false;
        furi_mutex_release(app->mutex);
        if(!active) break;
        furi_delay_ms(20);
    }
    settings_save(app);
    wav_discard_ready(app);
    furi_record_close(RECORD_STORAGE);
    if(app->speaker_owned) furi_hal_speaker_release();
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    furi_hal_power_insomnia_exit();
    cli_registry_delete_command(cli, "pmr_capture");
    furi_record_close(RECORD_CLI);
    view_port_enabled_set(app->view, false);
    gui_remove_view_port(gui, app->view);
    view_port_free(app->view);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
