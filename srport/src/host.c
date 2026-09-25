#include "host.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_RATE 44100
#define SPEAKER_AMPLITUDE 5000

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioStream *audio;
static SDL_Gamepad *gamepad;
static char *game_dir;

static void (*tick_handler)(void);
static void (*kbd_handler)(u8);
static void (*focus_lost_handler)(void);
static bool (*frame_source)(u32 *);
static u32 frame[HOST_FRAME_MAX_W * HOST_FRAME_MAX_H];
static int frame_w = 320, frame_h = 200;
#define VIEW_W(w) (w)                   /* logical presentation: frame width x 3/4 of it (4:3) */
#define VIEW_H(w) ((w) * 3 / 4)

/* Tick clock: since the last rate change (at clock_start_ns), tick n is due at
 * clock_start_ns + n * pit_div / 1193182 s (exact rational arithmetic). */
static Uint64 clock_start_ns;
static Uint64 ticks_run;
static u32 pit_div = PIT_DIV_GAME;

/* Audio: see the PC-speaker section below. */
static void run_tick(void);
static void audio_dump_from_env(void);
static void audio_dump_close(void);

/* Mouse: position in window-logical units and accumulated relative motion. */
static float mouse_x, mouse_y;
static float mouse_dx, mouse_dy;
static float mickey_x, mickey_y;

/* Developer aid SR_KEYS: scripted mouse (see host.h). */
static bool (*pointer_query)(s16 *x, s16 *y);
static u8 script_buttons;                   /* buttons held by the script, OR'd into host_mouse_read */

static void process_events(void);

bool host_init(const char *dir, int window_scale, bool fullscreen)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    game_dir = SDL_strdup(dir);
    if (window_scale < 1) window_scale = 3;
    if (!SDL_CreateWindowAndRenderer("Street Rod", 320 * window_scale, 240 * window_scale,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        fprintf(stderr, "window/renderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);
    if (fullscreen) SDL_SetWindowFullscreen(window, true);
    host_set_frame_source(NULL, 320, 200);

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, AUDIO_RATE };
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) {
        SDL_ResumeAudioStreamDevice(audio);
        static s16 silence[AUDIO_RATE / 20 * 2];           /* 50 ms of lead-in against underruns */
        SDL_PutAudioStreamData(audio, silence, sizeof silence);
    } else {
        fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    }
    audio_dump_from_env();

    clock_start_ns = SDL_GetTicksNS();
    ticks_run = 0;
    return true;
}

void host_shutdown(void)
{
    if (gamepad) SDL_CloseGamepad(gamepad);
    audio_dump_close();
    if (audio) SDL_DestroyAudioStream(audio);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_free(game_dir);
    SDL_Quit();
}

void host_set_tick_handler(void (*handler)(void)) { tick_handler = handler; }
void host_set_kbd_handler(void (*handler)(u8)) { kbd_handler = handler; }
void host_set_focus_lost_handler(void (*handler)(void)) { focus_lost_handler = handler; }

void host_set_frame_source(bool (*compose)(u32 *), int w, int h)
{
    frame_source = compose;
    frame_w = SDL_clamp(w, 1, HOST_FRAME_MAX_W);
    frame_h = SDL_clamp(h, 1, HOST_FRAME_MAX_H);
    /* The 200-line picture fills a 4:3 area, as it did on a VGA monitor. */
    SDL_SetRenderLogicalPresentation(renderer, VIEW_W(frame_w), VIEW_H(frame_w), SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (texture) SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, frame_w, frame_h);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
}

static Uint64 tick_due_ns(Uint64 n)
{
    return clock_start_ns + n * pit_div * SDL_NS_PER_SECOND / PIT_HZ;
}

void host_set_pit_divisor(u16 divisor)
{
    u32 d = divisor ? divisor : 0x10000u;
    if (d == pit_div) return;
    clock_start_ns = tick_due_ns(ticks_run);        /* the new rate starts at the last tick */
    ticks_run = 0;
    pit_div = d;
}

/* ---------------------------------------------------------------- PC speaker (audio)
 *
 * Emulated timeline, in PIT input clocks (PIT_HZ): tick n of the game timer starts at tl_tick and
 * everything the tick handler writes is stamped there; code outside the handler runs at tl_now =
 * tl_tick + the busy-wait time spent since that tick (host_busy_wait_ns). The speaker is modelled
 * at clock resolution: PIT channel 2 in mode 3 (square wave, count reloaded at every output
 * transition, restarted on a rising gate) and port 61h bit 0 (gate) / bit 1 (data):
 *     level = bit1 && (bit0 ? OUT2 : 1)
 * The 1-bit level is integrated exactly over each output sample (a box filter: pulses shorter than
 * a sample keep their energy, the 25 / 39.8 kHz "silent" carriers average out instead of
 * aliasing), then a DC blocker (the speaker is AC coupled; ~20 Hz, below the 25 Hz engine note)
 * and a one-pole low-pass (~10 kHz). Samples are rendered lazily: up to the stamp of each write, and
 * up to each tick boundary (so the device gets one tick of audio per tick, behind the timeline). */

#define HP_R  0.99715       /* 1 - 2*pi*20/44100: ~20 Hz DC blocker */
#define LP_A  0.7603        /* 1 - exp(-2*pi*10000/44100): ~10 kHz one-pole low-pass */
#define SPEAKER_GAIN (SPEAKER_AMPLITUDE * 2.0)   /* a full square swings +-SPEAKER_AMPLITUDE */
#define AUDIO_MAX_QUEUED  (AUDIO_RATE / 4)       /* samples; beyond that output is dropped */
#define AUDIO_LEAD_IN     (AUDIO_RATE / 25)      /* 40 ms of silence after an underrun */

static Uint64 tl_tick, tl_now;          /* PIT clocks */
static Uint64 tl_frac;                  /* ns->clock conversion remainder (units of 1/1e9 clock) */
static bool in_tick;                    /* tick handler running: busy waits do not nest ticks */
static bool realtime = true;

static u8 port61;
static u32 pit2_latch = 0x10000, pit2_count = 0x10000;
static bool out2 = true;
static Uint64 out2_next;                /* clock of the next OUT2 transition (gate on) */

static Uint64 r_pos;                    /* rendered up to, in clock * AUDIO_RATE units */
static Uint64 r_sample_end = PIT_HZ;    /* end of the sample being integrated (same units) */
static Uint64 r_acc;                    /* high time inside that sample (same units) */
static double hp_x1, hp_y1, lp_z;
static s16 r_buf[2048];
static int r_n;
static FILE *dump_f;
static u32 dump_samples;

static u32 half_period(u32 count, bool high)
{
    u32 h = high ? (count + 1) / 2 : count / 2;     /* mode 3, odd counts: high one clock longer */
    return h ? h : 1;
}

static void put_le(u8 *p, u32 v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (u8)(v >> (8 * i));
}

static void wav_header(FILE *f, u32 samples)     /* 16-bit mono PCM at AUDIO_RATE */
{
    u8 h[44];
    memcpy(h, "RIFF", 4);           put_le(h + 4, 36 + samples * 2, 4);
    memcpy(h + 8, "WAVEfmt ", 8);   put_le(h + 16, 16, 4);
    put_le(h + 20, 1, 2);           put_le(h + 22, 1, 2);
    put_le(h + 24, AUDIO_RATE, 4);  put_le(h + 28, AUDIO_RATE * 2, 4);
    put_le(h + 32, 2, 2);           put_le(h + 34, 16, 2);
    memcpy(h + 36, "data", 4);      put_le(h + 40, samples * 2, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, f);
    fseek(f, 0, SEEK_END);
}

static void audio_flush(void)
{
    if (r_n == 0) return;
    if (dump_f) { fwrite(r_buf, sizeof(s16), (size_t)r_n, dump_f); dump_samples += (u32)r_n; }
    if (audio) {
        int queued = SDL_GetAudioStreamQueued(audio) / (int)sizeof(s16);
        /* Drop output if the device is far behind (e.g. after a stall) instead of building latency. */
        if (queued <= AUDIO_MAX_QUEUED) {
            if (queued == 0) {                      /* underrun: rebuild a little latency */
                static const s16 silence[AUDIO_LEAD_IN];
                SDL_PutAudioStreamData(audio, silence, sizeof silence);
            }
            SDL_PutAudioStreamData(audio, r_buf, r_n * (int)sizeof(s16));
        }
    }
    r_n = 0;
}

static void emit_sample(double x)
{
    double y = x - hp_x1 + HP_R * hp_y1;
    hp_x1 = x;
    hp_y1 = y;
    lp_z += LP_A * (y - lp_z);
    double s = lp_z * SPEAKER_GAIN;
    r_buf[r_n++] = (s16)(s > 32767 ? 32767 : s < -32767 ? -32767 : s);
    if (r_n == (int)SDL_arraysize(r_buf)) audio_flush();
}

/* Renders the speaker output up to PIT clock clk (exclusive). */
static void render_to(Uint64 clk)
{
    Uint64 target = clk * AUDIO_RATE;
    while (r_pos < target) {
        bool gate = port61 & 1;
        Uint64 end = SDL_min(target, r_sample_end);
        if (gate) end = SDL_min(end, out2_next * AUDIO_RATE);
        if ((port61 & 2) && (!gate || out2)) r_acc += end - r_pos;
        r_pos = end;
        if (gate && r_pos == out2_next * AUDIO_RATE) {
            out2 = !out2;
            pit2_count = pit2_latch;                /* a new count takes effect at a transition */
            out2_next += half_period(pit2_count, out2);
        }
        if (r_pos == r_sample_end) {
            emit_sample((double)r_acc / PIT_HZ);
            r_acc = 0;
            r_sample_end += PIT_HZ;
        }
    }
}

/* One timer tick at the next tick boundary: audio up to it, then the tick handler (INT 8). */
static void run_tick(void)
{
    ticks_run++;
    Uint64 t = SDL_max(tl_tick + pit_div, tl_now);
    render_to(t);
    audio_flush();
    tl_tick = tl_now = t;
    in_tick = true;
    if (tick_handler) tick_handler();
    in_tick = false;
}

static void pace_until(Uint64 due_ns)
{
    for (;;) {
        Uint64 now = SDL_GetTicksNS();
        if (now >= due_ns) return;
        process_events();
        SDL_DelayPrecise(SDL_min(due_ns - now, SDL_NS_PER_MS));
    }
}

void host_pit2_divisor(u16 divisor)
{
    render_to(tl_now);
    pit2_latch = divisor ? divisor : 0x10000u;
}

void host_port61(u8 value)
{
    render_to(tl_now);
    bool was = port61 & 1, gate = value & 1;
    port61 = value;
    if (gate && !was) {                             /* rising gate restarts the square wave */
        out2 = true;
        pit2_count = pit2_latch;
        out2_next = tl_now + half_period(pit2_count, true);
    } else if (!gate) {
        out2 = true;                                /* mode 3: OUT2 goes high while the gate is low */
    }
}

u8 host_port61_get(void) { return port61; }

void host_speaker(u16 divisor, bool on)
{
    host_pit2_divisor(divisor);
    host_port61(on ? (u8)(port61 | 3) : (u8)(port61 & ~3));
}

void host_busy_wait_ns(Uint64 ns)
{
    Uint64 num = ns * PIT_HZ + tl_frac;
    Uint64 clk = num / SDL_NS_PER_SECOND;
    tl_frac = num % SDL_NS_PER_SECOND;
    if (in_tick) { tl_now += clk; return; }         /* (not expected: a busy wait inside the ISR) */
    for (;;) {
        Uint64 next = tl_tick + pit_div;
        if (tl_now + clk < next) { tl_now += clk; break; }
        clk -= next - tl_now;
        tl_now = next;
        if (realtime) pace_until(tick_due_ns(ticks_run + 1));
        run_tick();                                 /* the interrupt fires in the middle of the loop */
    }
    if (realtime) pace_until(tick_due_ns(ticks_run) + (tl_now - tl_tick) * SDL_NS_PER_SECOND / PIT_HZ);
}

void host_busy_wait_us(u32 us) { host_busy_wait_ns((Uint64)us * 1000); }

void host_set_realtime(bool on) { realtime = on; }

void host_run_tick_now(void) { run_tick(); }

Uint64 host_audio_clock(void) { return tl_now; }

bool host_audio_dump_begin(const char *wav_path)
{
    audio_dump_close();
    dump_f = fopen(wav_path, "wb");
    if (!dump_f) return false;
    dump_samples = 0;
    wav_header(dump_f, 0);
    return true;
}

void host_audio_dump_end(void) { audio_dump_close(); }

static void audio_dump_close(void)
{
    if (!dump_f) return;
    render_to(tl_now);
    audio_flush();
    wav_header(dump_f, dump_samples);
    fclose(dump_f);
    dump_f = NULL;
}

static void audio_dump_from_env(void)
{
    const char *path = SDL_getenv("SR_AUDIO_DUMP");
    if (path && *path && !host_audio_dump_begin(path)) fprintf(stderr, "cannot write %s\n", path);
}

/* Developer aid: SR_SNAPSHOT_DIR, see host.h. */
static void snapshot(void)
{
    static const char *dir;
    static bool checked;
    static Uint64 last_ns;
    static int n;
    static Uint64 interval_ns = 2 * SDL_NS_PER_SECOND;
    if (!checked) {
        dir = SDL_getenv("SR_SNAPSHOT_DIR");
        const char *ms = SDL_getenv("SR_SNAPSHOT_MS");
        if (ms && SDL_atoi(ms) > 0) interval_ns = (Uint64)SDL_atoi(ms) * SDL_NS_PER_MS;
        checked = true;
    }
    if (!dir) return;
    Uint64 now = SDL_GetTicksNS();
    if (n && now - last_ns < interval_ns) return;
    last_ns = now;
    SDL_Surface *s = SDL_CreateSurfaceFrom(frame_w, frame_h, SDL_PIXELFORMAT_XRGB8888, frame, frame_w * 4);
    if (!s) return;
    char path[512];
    SDL_snprintf(path, sizeof path, "%s/snap%04d.bmp", dir, n++);
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
}

static void present(void)
{
    snapshot();
    if (!texture) return;
    SDL_UpdateTexture(texture, NULL, frame, frame_w * 4);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_FRect dst = { 0, 0, (float)VIEW_W(frame_w), (float)VIEW_H(frame_w) };
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
}

static Uint64 last_present_ns;

/* Developer aid: SR_KEYS, see host.h. */
void host_set_pointer_query(bool (*query)(s16 *x, s16 *y)) { pointer_query = query; }

/* A scripted pointer action in progress: move to (tx, ty), then optionally press and release. */
static struct {
    int phase;                              /* 0 idle, 1 moving, 2 pressed */
    s16 tx, ty;
    u8 button;                              /* 0 = move only, 1 = left, 2 = right */
    Uint64 deadline_ns;                     /* move timeout, then the release time */
} ptr_act;

static double script_seconds(void) { return (double)(SDL_GetTicksNS() - clock_start_ns) / 1e9; }

/* Returns true while an action is still running (the next script entry waits for it). */
static bool pointer_action_step(void)
{
    Uint64 now = SDL_GetTicksNS();
    if (ptr_act.phase == 1) {
        s16 cx = 0, cy = 0;
        bool known = pointer_query && pointer_query(&cx, &cy);
        bool there = known && cx == ptr_act.tx && cy == ptr_act.ty;
        if (!there && known && now < ptr_act.deadline_ns) {
            /* Feed the difference once the game has taken the previous motion: 2 mickeys per game
             * pixel at the driver's default ratio (the game divides by 2, DS:05EA = 1). */
            if ((s16)mickey_x == 0 && (s16)mickey_y == 0) {
                mickey_x += (float)(ptr_act.tx - cx) * 2.0f;
                mickey_y += (float)(ptr_act.ty - cy) * 2.0f;
            }
            return true;
        }
        if (!there)
            fprintf(stderr, "SR_KEYS: pointer at (%d,%d), wanted (%d,%d)%s\n", cx, cy, ptr_act.tx, ptr_act.ty,
                    known ? "" : " (no pointer query)");
        if (!ptr_act.button) { ptr_act.phase = 0; return false; }
        script_buttons |= ptr_act.button;
        ptr_act.phase = 2;
        ptr_act.deadline_ns = now + 150 * SDL_NS_PER_MS;   /* hold like a real click */
        return true;
    }
    if (ptr_act.phase == 2) {
        if (now < ptr_act.deadline_ns) return true;
        script_buttons &= (u8)~ptr_act.button;
        ptr_act.phase = 0;
    }
    return false;
}

/* Mouse entries: m<x>.<y> move, c<x>.<y> / C<x>.<y> move + left / right click, l / r click in
 * place, lp / lr (rp / rr) hold / let go of the button. Returns the rest of the entry. */
static const char *script_mouse(const char *p)
{
    char kind = *p++;
    if (kind == 'm' || kind == 'c' || kind == 'C') {
        char *end;
        ptr_act.tx = (s16)SDL_strtol(p, &end, 10);
        p = end;
        if (*p == '.') p++;
        ptr_act.ty = (s16)SDL_strtol(p, &end, 10);
        p = end;
        ptr_act.button = kind == 'm' ? 0 : kind == 'c' ? 1 : 2;
        ptr_act.phase = 1;
        ptr_act.deadline_ns = SDL_GetTicksNS() + 3 * SDL_NS_PER_SECOND;
        return p;
    }
    u8 b = kind == 'l' ? 1 : 2;
    if (*p == 'p') { script_buttons |= b; return p + 1; }
    if (*p == 'r') { script_buttons &= (u8)~b; return p + 1; }
    script_buttons |= b;                     /* click in place */
    ptr_act.button = b;
    ptr_act.phase = 2;
    ptr_act.deadline_ns = SDL_GetTicksNS() + 150 * SDL_NS_PER_MS;
    return p;
}

static void scripted_keys(void)
{
    static const char *spec;
    static bool checked;
    if (!checked) { spec = SDL_getenv("SR_KEYS"); checked = true; }
    if (pointer_action_step()) return;
    if (!spec || !*spec || !kbd_handler) return;
    char *end;
    double at = SDL_strtod(spec, &end);
    if (end == spec || *end != ':') { spec = NULL; return; }
    if (script_seconds() < at) return;
    const char *p = end + 1;
    if (*p == 'm' || *p == 'c' || *p == 'C' || *p == 'l' || *p == 'r') {
        p = script_mouse(p);
        spec = *p == ',' ? p + 1 : NULL;
        return;
    }
    u16 keys[8];
    int n = 0;
    bool press = true, release = true;
    while (n < 8) {
        keys[n++] = (u16)SDL_strtoul(p, &end, 16);
        p = end;
        if (*p == 'p') { release = false; p++; }        /* held down */
        else if (*p == 'r') { press = false; p++; }      /* let go */
        if (*p != '+') break;
        p++;
    }
    if (press)
        for (int i = 0; i < n; i++) {
            if (keys[i] >> 8) kbd_handler((u8)(keys[i] >> 8));
            kbd_handler((u8)keys[i]);
        }
    if (release)
        for (int i = n - 1; i >= 0; i--) {
            if (keys[i] >> 8) kbd_handler((u8)(keys[i] >> 8));
            kbd_handler((u8)(keys[i] | 0x80));
        }
    spec = *p == ',' ? p + 1 : NULL;
}

void host_pump(void)
{
    process_events();
    scripted_keys();

    bool worked = false;
    Uint64 now = SDL_GetTicksNS();
    /* at most 0.5 s of catch-up per call */
    int budget = (int)(PIT_HZ / 2 / pit_div) + 1;
    while (tick_due_ns(ticks_run + 1) <= now && budget-- > 0) {
        run_tick();                                 /* ticks_run++, audio up to the tick, handler */
        worked = true;
    }
    if (budget < 0) {                               /* fell too far behind: resynchronise the clock */
        clock_start_ns = now - (tick_due_ns(ticks_run) - clock_start_ns);
    }

    /* Present at most once per ~8 ms; VSync paces it further. */
    if (frame_source && now - last_present_ns >= 8 * SDL_NS_PER_MS) {
        if (frame_source(frame)) {
            present();
            last_present_ns = SDL_GetTicksNS();
            worked = true;
        }
    }
    if (!worked) {
        Uint64 next = tick_due_ns(ticks_run + 1);
        now = SDL_GetTicksNS();
        if (next > now) SDL_DelayNS(SDL_min(next - now, SDL_NS_PER_MS));
    }
}

/* VGA 320x200 (mode 0Dh on a VGA) refresh: 25.175 MHz / (800 x 449) = 70.086 Hz. */
#define VRETRACE_NUM (800ull * 449ull * SDL_NS_PER_SECOND)
#define VRETRACE_DEN (25175000ull)

#define VRETRACE_WINDOW_NS 1500000ull     /* 49 blank lines x 31.8 us */

bool host_in_vretrace(void)
{
    Uint64 since = SDL_GetTicksNS() - clock_start_ns;
    Uint64 frame_start = since * VRETRACE_DEN / VRETRACE_NUM * VRETRACE_NUM / VRETRACE_DEN;
    return since - frame_start < VRETRACE_WINDOW_NS;
}

void host_wait_vretrace(void)
{
    Uint64 since = SDL_GetTicksNS() - clock_start_ns;
    Uint64 next = (since * VRETRACE_DEN / VRETRACE_NUM + 1) * VRETRACE_NUM / VRETRACE_DEN;
    while (SDL_GetTicksNS() - clock_start_ns < next) host_pump();
}


/* ---------------------------------------------------------------- keyboard */

/* XT set-1 make code of an SDL key; bit 8 set = grey key sent with an E0 prefix. 0 = not reported.
 * (Keypad digits send their plain codes, the grey block its E0 codes.) */
#define GREY 0x100
static u16 xt_scan(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        static const u8 letter_scan[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
                                            0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
        return letter_scan[sc - SDL_SCANCODE_A];
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return (u16)(0x02 + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return (u16)(0x3B + (sc - SDL_SCANCODE_F1));
    switch (sc) {
    case SDL_SCANCODE_F11:          return 0x57;
    case SDL_SCANCODE_F12:          return 0x58;
    case SDL_SCANCODE_ESCAPE:       return 0x01;
    case SDL_SCANCODE_MINUS:        return 0x0C;
    case SDL_SCANCODE_EQUALS:       return 0x0D;
    case SDL_SCANCODE_BACKSPACE:    return 0x0E;
    case SDL_SCANCODE_TAB:          return 0x0F;
    case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN:       return 0x1C;
    case SDL_SCANCODE_KP_ENTER:     return GREY | 0x1C;
    case SDL_SCANCODE_LCTRL:        return 0x1D;
    case SDL_SCANCODE_RCTRL:        return GREY | 0x1D;
    case SDL_SCANCODE_SEMICOLON:    return 0x27;
    case SDL_SCANCODE_APOSTROPHE:   return 0x28;
    case SDL_SCANCODE_GRAVE:        return 0x29;
    case SDL_SCANCODE_LSHIFT:       return 0x2A;
    case SDL_SCANCODE_BACKSLASH:    return 0x2B;
    case SDL_SCANCODE_COMMA:        return 0x33;
    case SDL_SCANCODE_PERIOD:       return 0x34;
    case SDL_SCANCODE_SLASH:        return 0x35;
    case SDL_SCANCODE_KP_DIVIDE:    return GREY | 0x35;
    case SDL_SCANCODE_RSHIFT:       return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY:  return 0x37;
    case SDL_SCANCODE_LALT:         return 0x38;
    case SDL_SCANCODE_RALT:         return GREY | 0x38;
    case SDL_SCANCODE_SPACE:        return 0x39;
    case SDL_SCANCODE_CAPSLOCK:     return 0x3A;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_SCROLLLOCK:   return 0x46;
    case SDL_SCANCODE_KP_7:         return 0x47;
    case SDL_SCANCODE_KP_8:         return 0x48;
    case SDL_SCANCODE_KP_9:         return 0x49;
    case SDL_SCANCODE_KP_MINUS:     return 0x4A;
    case SDL_SCANCODE_KP_4:         return 0x4B;
    case SDL_SCANCODE_KP_5:         return 0x4C;
    case SDL_SCANCODE_KP_6:         return 0x4D;
    case SDL_SCANCODE_KP_PLUS:      return 0x4E;
    case SDL_SCANCODE_KP_1:         return 0x4F;
    case SDL_SCANCODE_KP_2:         return 0x50;
    case SDL_SCANCODE_KP_3:         return 0x51;
    case SDL_SCANCODE_KP_0:         return 0x52;
    case SDL_SCANCODE_KP_PERIOD:    return 0x53;
    case SDL_SCANCODE_HOME:         return GREY | 0x47;
    case SDL_SCANCODE_UP:           return GREY | 0x48;
    case SDL_SCANCODE_PAGEUP:       return GREY | 0x49;
    case SDL_SCANCODE_LEFT:         return GREY | 0x4B;
    case SDL_SCANCODE_RIGHT:        return GREY | 0x4D;
    case SDL_SCANCODE_END:          return GREY | 0x4F;
    case SDL_SCANCODE_DOWN:         return GREY | 0x50;
    case SDL_SCANCODE_PAGEDOWN:     return GREY | 0x51;
    case SDL_SCANCODE_INSERT:       return GREY | 0x52;
    case SDL_SCANCODE_DELETE:       return GREY | 0x53;
    default:                        return 0;
    }
}

static void feed(const u8 *bytes, int n)
{
    if (!kbd_handler) return;
    for (int i = 0; i < n; i++) kbd_handler(bytes[i]);
}

static void key_event(SDL_Scancode sc, bool down)
{
    if (sc == SDL_SCANCODE_PAUSE) {                 /* make only, no break sequence */
        static const u8 pause[] = { 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 };
        if (down) feed(pause, sizeof pause);
        return;
    }
    if (sc == SDL_SCANCODE_PRINTSCREEN) {
        static const u8 make[] = { 0xE0, 0x2A, 0xE0, 0x37 }, brk[] = { 0xE0, 0xB7, 0xE0, 0xAA };
        if (down) feed(make, sizeof make); else feed(brk, sizeof brk);
        return;
    }
    u16 x = xt_scan(sc);
    if (!x) return;
    u8 seq[2];
    int n = 0;
    if (x & GREY) seq[n++] = 0xE0;
    seq[n++] = (u8)(down ? (x & 0x7F) : (x & 0x7F) | 0x80);
    feed(seq, n);
}

static void process_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            host_shutdown();
            exit(0);
        case SDL_EVENT_KEY_DOWN:
            /* Alt+Enter toggles fullscreen; everything else goes to the game, repeats included. */
            if (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                if (!ev.key.repeat)
                    SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                break;
            }
            key_event(ev.key.scancode, true);
            break;
        case SDL_EVENT_KEY_UP:
            key_event(ev.key.scancode, false);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (focus_lost_handler) focus_lost_handler();
            break;
        case SDL_EVENT_MOUSE_MOTION:
            mouse_dx += ev.motion.xrel;             /* mickeys: window pixels, as the driver's default */
            mouse_dy += ev.motion.yrel;
            {
                /* game picture size in window coordinates (4:3 letterbox) */
                int ww = 0, wh = 0;
                SDL_GetWindowSize(window, &ww, &wh);
                float vw = (float)ww, vh = (float)wh;
                if (vw * 3 > vh * 4) vw = vh * 4 / 3; else vh = vw * 3 / 4;
                if (vw >= 1 && vh >= 1) {
                    mickey_x += ev.motion.xrel * 2.0f * (float)frame_w / vw;
                    mickey_y += ev.motion.yrel * 2.0f * (float)frame_h / vh;
                }
            }
            SDL_ConvertEventToRenderCoordinates(renderer, &ev);
            mouse_x = ev.motion.x;
            mouse_y = ev.motion.y;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad) gamepad = SDL_OpenGamepad(ev.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad && SDL_GetGamepadID(gamepad) == ev.gdevice.which) {
                SDL_CloseGamepad(gamepad);
                gamepad = NULL;
            }
            break;
        default:
            break;
        }
    }
}

/* ---------------------------------------------------------------- mouse, joystick */

void host_mouse_read(s16 *x, s16 *y, u8 *buttons)
{
    process_events();
    /* Logical presentation is 320 x 240 (4:3); the game's screen is 320 x 200. */
    float px = SDL_clamp(mouse_x, 0.0f, (float)VIEW_W(frame_w) - 1);
    float py = SDL_clamp(mouse_y, 0.0f, (float)VIEW_H(frame_w) - 1);
    if (x) *x = (s16)(px * frame_w / VIEW_W(frame_w));
    if (y) *y = (s16)(py * frame_h / VIEW_H(frame_w));
    SDL_MouseButtonFlags b = SDL_GetMouseState(NULL, NULL);
    if (buttons) *buttons = (u8)(((b & SDL_BUTTON_LMASK) ? 1 : 0) | ((b & SDL_BUTTON_RMASK) ? 2 : 0) | script_buttons);
}

void host_mouse_motion(s16 *dx, s16 *dy)
{
    process_events();
    if (dx) *dx = (s16)mouse_dx;
    if (dy) *dy = (s16)mouse_dy;
    mouse_dx -= (s16)mouse_dx;
    mouse_dy -= (s16)mouse_dy;
}

void host_mouse_mickeys(s16 *mx, s16 *my)
{
    process_events();
    s16 x = (s16)mickey_x, y = (s16)mickey_y;
    mickey_x -= x;
    mickey_y -= y;
    if (mx) *mx = x;
    if (my) *my = y;
}

void host_mouse_set_relative(bool on)
{
    if (window) SDL_SetWindowRelativeMouseMode(window, on);
}

bool host_joy_read(s16 *x, s16 *y, u8 *buttons)
{
    if (!gamepad) return false;
    s16 ax = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    s16 ay = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  ax = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) ax = 32767;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))    ay = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  ay = 32767;
    u8 b = 0;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) b |= 1;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST))  b |= 2;
    if (x) *x = ax;
    if (y) *y = ay;
    if (buttons) *buttons = b;
    return true;
}

/* ---------------------------------------------------------------- files, errors */

char *host_game_path(const char *name, bool create)
{
    char *direct = NULL;
    SDL_asprintf(&direct, "%s/%s", game_dir, name);
    if (SDL_GetPathInfo(direct, NULL)) return direct;

    int count = 0;
    char **entries = SDL_GlobDirectory(game_dir, NULL, 0, &count);
    char *found = NULL;
    for (int i = 0; entries && i < count; i++) {
        if (SDL_strcasecmp(entries[i], name) == 0) {
            SDL_asprintf(&found, "%s/%s", game_dir, entries[i]);
            break;
        }
    }
    SDL_free(entries);
    if (found) { SDL_free(direct); return found; }
    if (create) return direct;
    SDL_free(direct);
    return NULL;
}

void host_free(void *p)
{
    SDL_free(p);
}

_Noreturn void host_fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "fatal: %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Street Rod", msg, window);
    host_shutdown();
    exit(3);
}
