#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_gpio.h>
#include <furi_hal_pwm.h>
#include <furi_hal_subghz.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

#define CONFIG_PATH APP_DATA_PATH("config.txt")
#define OUTPUT_PATH APP_DATA_PATH("samples.txt")

typedef enum {
    ModePipe = 0,
    ModeTree = 1,
} SimMode;

typedef enum {
    OutNone = 0,
    OutGpio = 1,
    OutSubGhz = 2,
} OutputTarget;

typedef enum {
    ScreenSim = 0,
    ScreenSettings = 1,
} ScreenState;

typedef enum {
    SettingMode = 0,
    SettingOutput,
    SettingSubGhzFreq,
    SettingRate,
    SettingSamples,
    SettingFreq,
    SettingAmp,
    SettingInductanceUh,
    SettingCapacitanceUf,
    SettingAlpha,
    SettingSaveExport,
    SettingCount
} SettingField;

typedef struct {
    SimMode mode;
    OutputTarget output;
    uint32_t subghz_freq_hz;
    float sample_rate;
    uint32_t num_samples;
    float freq_hz;
    float amplitude;
    float inductance_h;
    float capacitance_f;
    float alpha;
} SimConfig;

typedef struct {
    FuriMutex* mutex;
    SimConfig cfg;
    ScreenState screen;
    SettingField selected;
    float last_signal;
    float last_current;
    bool pwm_active;
    bool rf_tx_active;
    char status_msg[32];
} AppState;

static void stop_hardware_outputs(AppState* state) {
    if(state->pwm_active) {
        furi_hal_pwm_stop(FuriHalPwmOutputIdTim1PA7);
        furi_hal_gpio_init(&gpio_ext_pa7, GpioModeInput, GpioPullNo, GpioSpeedLow);
        state->pwm_active = false;
    }
    if(state->rf_tx_active) {
        furi_hal_subghz_sleep();
        state->rf_tx_active = false;
    }
}

static void drive_sample_hardware(AppState* state, float signal) {
    if(state->cfg.output == OutNone) {
        return;
    }

    float scale = (state->cfg.amplitude > 0.01f) ? state->cfg.amplitude : 5.0f;
    float norm = (signal / scale + 1.0f) * 0.5f;
    if(norm < 0.0f) norm = 0.0f;
    if(norm > 1.0f) norm = 1.0f;
    uint8_t duty = (uint8_t)(norm * 100.0f);

    if(state->cfg.output == OutGpio) {
        uint32_t pwm_freq = (state->cfg.freq_hz >= 100.0f) ? (uint32_t)state->cfg.freq_hz : 1000U;
        if(!state->pwm_active) {
            furi_hal_pwm_start(FuriHalPwmOutputIdTim1PA7, pwm_freq, duty);
            state->pwm_active = true;
        } else {
            furi_hal_pwm_set_params(FuriHalPwmOutputIdTim1PA7, pwm_freq, duty);
        }
    } else if(state->cfg.output == OutSubGhz) {
        if(!state->rf_tx_active) {
            furi_hal_subghz_reset();
            uint32_t actual_hz = furi_hal_subghz_set_frequency_and_path(state->cfg.subghz_freq_hz);
            if(actual_hz > 0U && furi_hal_subghz_tx()) {
                state->rf_tx_active = true;
            }
        }
        UNUSED(duty);
    }
}

static void config_set_defaults(SimConfig* cfg) {
    cfg->mode = ModeTree;
    cfg->output = OutNone;
    cfg->subghz_freq_hz = 433920000U;
    cfg->sample_rate = 200000.0f;
    cfg->num_samples = 40;
    cfg->freq_hz = 22000.0f;
    cfg->amplitude = 5.0f;
    cfg->inductance_h = 470e-6f;
    cfg->capacitance_f = 1e-6f;
    cfg->alpha = 0.05f;
}

static void save_config(const SimConfig* cfg) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, CONFIG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[300];
        int len = snprintf(
            buf,
            sizeof(buf),
            "mode=%d\nout=%d\nsubghz=%lu\nrate=%.1f\nsamples=%lu\nfreq=%.1f\namp=%.4f\nind=%.9e\ncap=%.9e\nalpha=%.4f\n",
            (int)cfg->mode,
            (int)cfg->output,
            (unsigned long)cfg->subghz_freq_hz,
            (double)cfg->sample_rate,
            (unsigned long)cfg->num_samples,
            (double)cfg->freq_hz,
            (double)cfg->amplitude,
            (double)cfg->inductance_h,
            (double)cfg->capacitance_f,
            (double)cfg->alpha);
        if(len > 0) {
            storage_file_write(file, buf, (uint16_t)len);
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void parse_config_line(SimConfig* cfg, const char* line) {
    int i_val = 0;
    unsigned long ul_val = 0;
    float f_val = 0.0f;

    if(sscanf(line, "mode=%d", &i_val) == 1) {
        cfg->mode = (i_val == 1) ? ModeTree : ModePipe;
    } else if(sscanf(line, "out=%d", &i_val) == 1) {
        if(i_val == OutGpio) cfg->output = OutGpio;
        else if(i_val == OutSubGhz) cfg->output = OutSubGhz;
        else cfg->output = OutNone;
    } else if(sscanf(line, "subghz=%lu", &ul_val) == 1 && ul_val >= 300000000UL && ul_val <= 928000000UL) {
        cfg->subghz_freq_hz = (uint32_t)ul_val;
    } else if(sscanf(line, "rate=%f", &f_val) == 1 && f_val > 0.0f && isfinite(f_val)) {
        cfg->sample_rate = f_val;
    } else if(sscanf(line, "samples=%lu", &ul_val) == 1 && ul_val > 0 && ul_val <= 4096) {
        cfg->num_samples = (uint32_t)ul_val;
    } else if(sscanf(line, "freq=%f", &f_val) == 1 && f_val >= 0.0f && isfinite(f_val)) {
        cfg->freq_hz = f_val;
    } else if(sscanf(line, "amp=%f", &f_val) == 1 && isfinite(f_val)) {
        cfg->amplitude = f_val;
    } else if(sscanf(line, "ind=%f", &f_val) == 1 && f_val != 0.0f && isfinite(f_val)) {
        cfg->inductance_h = f_val;
    } else if(sscanf(line, "cap=%f", &f_val) == 1 && isfinite(f_val)) {
        cfg->capacitance_f = f_val;
    } else if(sscanf(line, "alpha=%f", &f_val) == 1 && f_val > 0.0f && f_val < 1.0f) {
        cfg->alpha = f_val;
    }
}

static void load_config(SimConfig* cfg) {
    config_set_defaults(cfg);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, CONFIG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[300];
        uint16_t read_bytes = storage_file_read(file, buf, sizeof(buf) - 1);
        buf[read_bytes] = '\0';

        char* cursor = buf;
        while(*cursor != '\0') {
            char* newline = strchr(cursor, '\n');
            if(newline != NULL) {
                *newline = '\0';
            }
            if(*cursor != '\0') {
                parse_config_line(cfg, cursor);
            }
            if(newline == NULL) {
                break;
            }
            cursor = newline + 1;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void run_simulation(AppState* state, bool write_file) {
    stop_hardware_outputs(state);

    SimConfig cfg = state->cfg;
    float dt = 1.0f / cfg.sample_rate;
    if(!isfinite(dt) || dt <= 0.0f) {
        snprintf(state->status_msg, sizeof(state->status_msg), "Invalid dt");
        return;
    }

    Storage* storage = NULL;
    File* out_file = NULL;
    if(write_file) {
        storage = furi_record_open(RECORD_STORAGE);
        storage_simply_mkdir(storage, APP_DATA_PATH(""));
        out_file = storage_file_alloc(storage);
        if(!storage_file_open(out_file, OUTPUT_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            storage_file_free(out_file);
            furi_record_close(RECORD_STORAGE);
            snprintf(state->status_msg, sizeof(state->status_msg), "SD write err");
            return;
        }
    }

    float last_sig = 0.0f;
    float last_cur = 0.0f;

    if(cfg.mode == ModePipe) {
        float flux = 0.0f;
        float prev = 0.0f;
        float dc = 0.0f;
        for(uint32_t i = 0; i < cfg.num_samples; i++) {
            float t = (float)i / cfg.sample_rate;
            float signal = 0.0f;
            float current = 0.0f;

            float wave = cfg.amplitude * sinf(2.0f * M_PIf * cfg.freq_hz * t);
            signal += wave;
            current += wave * 0.01f;

            flux += (signal / cfg.inductance_h) * dt * 1e6f;
            current = flux;

            dc = (1.0f - cfg.alpha) * dc + cfg.alpha * signal;
            float dv = signal - prev;
            current = cfg.capacitance_f * (dv / dt);
            prev = signal;
            signal -= dc;

            if(isfinite(t) && isfinite(signal) && isfinite(current)) {
                last_sig = signal;
                last_cur = current;
                drive_sample_hardware(state, signal);
                if(out_file) {
                    char line[96];
                    int n = snprintf(
                        line,
                        sizeof(line),
                        "%.9f %+.6f %+.6f\n",
                        (double)t,
                        (double)signal,
                        (double)current);
                    if(n > 0) storage_file_write(out_file, line, (uint16_t)n);
                }
            }
        }
    } else {
        float prev1 = 0.0f, dc1 = 0.0f, flux1 = 0.0f;
        float prev2 = 0.0f, dc2 = 0.0f, flux2 = 0.0f;
        for(uint32_t i = 0; i < cfg.num_samples; i++) {
            float t = (float)i / cfg.sample_rate;

            float drive_a = cfg.amplitude * sinf(2.0f * M_PIf * cfg.freq_hz * t);
            flux1 += (drive_a / cfg.inductance_h) * dt * 1e6f;
            float after_l1 = flux1 * 0.001f;
            dc1 = (1.0f - cfg.alpha) * dc1 + cfg.alpha * after_l1;
            float dv1 = after_l1 - prev1;
            float i_c1 = cfg.capacitance_f * (dv1 / dt);
            prev1 = after_l1;
            float sig_a = after_l1 - dc1;

            float drive_b = (cfg.amplitude * 0.6f) * sinf(2.0f * M_PIf * cfg.freq_hz * t);
            flux2 += (drive_b / cfg.inductance_h) * dt * 1e6f;
            float after_l2 = flux2 * 0.001f;
            dc2 = (1.0f - cfg.alpha) * dc2 + cfg.alpha * after_l2;
            float dv2 = after_l2 - prev2;
            float i_c2 = cfg.capacitance_f * (dv2 / dt);
            prev2 = after_l2;
            float sig_b = after_l2 - dc2;

            float signal = sig_a * 0.6f + sig_b * 0.4f + 0.5f * sinf(2.0f * M_PIf * cfg.freq_hz * t);
            float current = i_c1 + i_c2;

            if(isfinite(t) && isfinite(signal) && isfinite(current)) {
                last_sig = signal;
                last_cur = current;
                drive_sample_hardware(state, signal);
                if(out_file) {
                    char line[96];
                    int n = snprintf(
                        line,
                        sizeof(line),
                        "%.9f %+.6f %+.6f\n",
                        (double)t,
                        (double)signal,
                        (double)current);
                    if(n > 0) storage_file_write(out_file, line, (uint16_t)n);
                }
            }
        }
    }

    if(out_file) {
        storage_file_close(out_file);
        storage_file_free(out_file);
        furi_record_close(RECORD_STORAGE);
        snprintf(state->status_msg, sizeof(state->status_msg), "Saved samples.txt");
    } else {
        const char* out_name = (cfg.output == OutGpio) ? "GPIO" :
                               (cfg.output == OutSubGhz) ? "RF" : "SIM";
        snprintf(state->status_msg, sizeof(state->status_msg), "OK: %s", out_name);
    }

    stop_hardware_outputs(state);
    state->last_signal = last_sig;
    state->last_current = last_cur;
}

static void adjust_setting(SimConfig* cfg, SettingField field, int dir) {
    switch(field) {
    case SettingMode:
        cfg->mode = (cfg->mode == ModeTree) ? ModePipe : ModeTree;
        break;
    case SettingOutput: {
        int next = ((int)cfg->output + (dir > 0 ? 1 : 2)) % 3;
        cfg->output = (OutputTarget)next;
        break;
    }
    case SettingSubGhzFreq:
        cfg->subghz_freq_hz = (cfg->subghz_freq_hz == 433920000U) ? 915000000U : 433920000U;
        break;
    case SettingRate: {
        float next = cfg->sample_rate + (dir > 0 ? 50000.0f : -50000.0f);
        if(next >= 10000.0f && next <= 1000000.0f) cfg->sample_rate = next;
        break;
    }
    case SettingSamples: {
        int next = (int)cfg->num_samples + (dir > 0 ? 10 : -10);
        if(next >= 10 && next <= 1000) cfg->num_samples = (uint32_t)next;
        break;
    }
    case SettingFreq: {
        float next = cfg->freq_hz + (dir > 0 ? 1000.0f : -1000.0f);
        if(next >= 100.0f && next <= 100000.0f) cfg->freq_hz = next;
        break;
    }
    case SettingAmp: {
        float next = cfg->amplitude + (dir > 0 ? 0.5f : -0.5f);
        if(next >= 0.0f && next <= 50.0f) cfg->amplitude = next;
        break;
    }
    case SettingInductanceUh: {
        float uh = cfg->inductance_h * 1e6f + (dir > 0 ? 10.0f : -10.0f);
        if(uh >= 10.0f && uh <= 10000.0f) cfg->inductance_h = uh * 1e-6f;
        break;
    }
    case SettingCapacitanceUf: {
        float uf = cfg->capacitance_f * 1e6f + (dir > 0 ? 0.1f : -0.1f);
        if(uf >= 0.1f && uf <= 100.0f) cfg->capacitance_f = uf * 1e-6f;
        break;
    }
    case SettingAlpha: {
        float next = cfg->alpha + (dir > 0 ? 0.01f : -0.01f);
        if(next >= 0.01f && next <= 0.99f) cfg->alpha = next;
        break;
    }
    default:
        break;
    }
}

static void draw_callback(Canvas* canvas, void* ctx) {
    AppState* state = ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(state->screen == ScreenSim) {
        const char* out_lbl = (state->cfg.output == OutGpio) ? "GPIO" :
                              (state->cfg.output == OutSubGhz) ? "RF" : "OFF";
        char title[48];
        snprintf(
            title,
            sizeof(title),
            "Signal [%s] Out:%s",
            state->cfg.mode == ModeTree ? "TREE" : "PIPE",
            out_lbl);
        canvas_draw_str(canvas, 2, 11, title);

        canvas_set_font(canvas, FontSecondary);
        char line1[48];
        snprintf(
            line1,
            sizeof(line1),
            "Fs:%.0fHz N:%lu F:%.0fHz",
            (double)state->cfg.sample_rate,
            (unsigned long)state->cfg.num_samples,
            (double)state->cfg.freq_hz);
        canvas_draw_str(canvas, 2, 23, line1);

        char line2[48];
        snprintf(
            line2,
            sizeof(line2),
            "Sig:%+.3f Cur:%+.3f",
            (double)state->last_signal,
            (double)state->last_current);
        canvas_draw_str(canvas, 2, 34, line2);

        char line3[48];
        snprintf(line3, sizeof(line3), "Status: %s", state->status_msg);
        canvas_draw_str(canvas, 2, 45, line3);

        canvas_draw_str(canvas, 2, 62, "[OK]Run+Exp [Dn]Set [Back]Exit");
    } else {
        canvas_draw_str(canvas, 2, 11, "Settings (Up/Dn Sel, L/R Adj)");
        canvas_set_font(canvas, FontSecondary);

        int start = (int)state->selected - 1;
        if(start < 0) start = 0;
        if(start > SettingCount - 4) start = SettingCount - 4;
        if(start < 0) start = 0;

        for(int row = 0; row < 4 && (start + row) < SettingCount; row++) {
            SettingField f = (SettingField)(start + row);
            int y = 23 + row * 10;
            const char* prefix = (f == state->selected) ? "> " : "  ";
            char buf[54];

            switch(f) {
            case SettingMode:
                snprintf(
                    buf, sizeof(buf), "%sMode: %s", prefix, state->cfg.mode == ModeTree ? "TREE" : "PIPE");
                break;
            case SettingOutput: {
                const char* o = (state->cfg.output == OutGpio) ? "GPIO(PA7)" :
                                (state->cfg.output == OutSubGhz) ? "SUBGHZ" : "NONE";
                snprintf(buf, sizeof(buf), "%sOut: %s", prefix, o);
                break;
            }
            case SettingSubGhzFreq:
                snprintf(
                    buf,
                    sizeof(buf),
                    "%sRF: %.2f MHz",
                    prefix,
                    (double)(state->cfg.subghz_freq_hz / 1000000.0f));
                break;
            case SettingRate:
                snprintf(buf, sizeof(buf), "%sRate: %.0f Hz", prefix, (double)state->cfg.sample_rate);
                break;
            case SettingSamples:
                snprintf(buf, sizeof(buf), "%sSamples: %lu", prefix, (unsigned long)state->cfg.num_samples);
                break;
            case SettingFreq:
                snprintf(buf, sizeof(buf), "%sFreq: %.0f Hz", prefix, (double)state->cfg.freq_hz);
                break;
            case SettingAmp:
                snprintf(buf, sizeof(buf), "%sAmp: %.1f", prefix, (double)state->cfg.amplitude);
                break;
            case SettingInductanceUh:
                snprintf(
                    buf, sizeof(buf), "%sL: %.0f uH", prefix, (double)(state->cfg.inductance_h * 1e6f));
                break;
            case SettingCapacitanceUf:
                snprintf(
                    buf, sizeof(buf), "%sC: %.2f uF", prefix, (double)(state->cfg.capacitance_f * 1e6f));
                break;
            case SettingAlpha:
                snprintf(buf, sizeof(buf), "%sAlpha: %.2f", prefix, (double)state->cfg.alpha);
                break;
            case SettingSaveExport:
                snprintf(buf, sizeof(buf), "%s[OK] Save & Export SD", prefix);
                break;
            default:
                buf[0] = '\0';
                break;
            }
            canvas_draw_str(canvas, 2, y, buf);
        }
    }

    furi_mutex_release(state->mutex);
}

static void input_callback(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        furi_message_queue_put(queue, event, 0);
    }
}

int32_t signal_sim_app(void* p) {
    UNUSED(p);

    AppState* state = malloc(sizeof(AppState));
    memset(state, 0, sizeof(AppState));
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    load_config(&state->cfg);
    state->screen = ScreenSim;
    state->selected = SettingMode;
    run_simulation(state, false);

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, state);
    view_port_input_callback_set(view_port, input_callback, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    bool running = true;
    InputEvent event;
    while(running) {
        if(furi_message_queue_get(queue, &event, 100) == FuriStatusOk) {
            furi_mutex_acquire(state->mutex, FuriWaitForever);
            if(state->screen == ScreenSim) {
                if(event.key == InputKeyBack) {
                    running = false;
                } else if(event.key == InputKeyDown) {
                    state->screen = ScreenSettings;
                } else if(event.key == InputKeyOk) {
                    run_simulation(state, true);
                } else if(event.key == InputKeyRight || event.key == InputKeyLeft) {
                    state->cfg.mode = (state->cfg.mode == ModeTree) ? ModePipe : ModeTree;
                    save_config(&state->cfg);
                    run_simulation(state, false);
                }
            } else {
                if(event.key == InputKeyBack) {
                    save_config(&state->cfg);
                    run_simulation(state, false);
                    state->screen = ScreenSim;
                } else if(event.key == InputKeyUp) {
                    if(state->selected == 0) state->selected = SettingCount - 1;
                    else state->selected--;
                } else if(event.key == InputKeyDown) {
                    state->selected = (state->selected + 1) % SettingCount;
                } else if(event.key == InputKeyLeft) {
                    adjust_setting(&state->cfg, state->selected, -1);
                } else if(event.key == InputKeyRight) {
                    adjust_setting(&state->cfg, state->selected, 1);
                } else if(event.key == InputKeyOk) {
                    save_config(&state->cfg);
                    run_simulation(state, true);
                    state->screen = ScreenSim;
                }
            }
            furi_mutex_release(state->mutex);
            view_port_update(view_port);
        }
    }

    stop_hardware_outputs(state);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(queue);
    furi_mutex_free(state->mutex);
    free(state);
    return 0;
}
