/* srsnd-dump: developer check of the sound module against tools/srsnd.py (port/spec/sound.md).
 *
 * usage: srsnd-dump GAME_DIR OUT_DIR
 *
 * Runs the port's sound code offline (no window, no audio device, no real-time pacing) through the
 * host speaker model and writes to OUT_DIR:
 *   trace_*.txt      per tick of each song: DS_vib_last_div and DS_speaker_on after the tick
 *   songs.txt        event list of every song, same line format as tools/srsnd.py's songs.txt
 *                    (compare: python tools/snd_compare.py work/SR_unp.exe OUT_DIR)
 *   engine.csv       RPM -> divisor, same format as srsnd.py's engine.csv (must be identical)
 *   song_*.wav       the background tune (one pass) and the loop sounds (3 passes)
 *   sfx_*.wav        the blocking effects; their durations are printed
 *   siren.wav        siren + idle engine through the drive-ISR sound part
 *   engine_sweep.wav 1500 -> 5700 RPM over 16 s (as srsnd.py)
 *   race_hit.wav     fx_hit and fx_crash while the drive ISR runs (ticks inside the effects)
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "mem.h"
#include "symbols.h"
#include "sound/sound.h"
#include "sound/snd_int.h"

/* The platform's click_clear 0000:0d78 (memory only), which the sound module calls; this tool links
 * without the platform module. */
void click_clear(void)
{
    DSB(DS_cursor_moved) = 1;
    DSW(0x8B92) = 0xFFFF;
    DSW(0x8BAA) = 0x7D00;
    DSB(DS_click_code) = 0;
    DSB(DS_click2_code) = 0;
    DSB(DS_key) = 0;
}

static const char *game_dir, *out_dir;

static void load(void)
{
    char path[1024], err[256];
    snprintf(path, sizeof path, "%s/%s", game_dir, SR_EXE_NAME);
    if (!mem_load_exe(path, err, sizeof err)) { fprintf(stderr, "%s\n", err); exit(1); }
    snd_song_defaults();                        /* platform_install's part */
    snd_init();
}

static void dump_begin(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", out_dir, name);
    if (!host_audio_dump_begin(path)) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
}

/* ---- song event log (the format of tools/srsnd.py) */

static FILE *songs_txt, *ev;            /* ev: events of the song being run */
static u16 log_song;
static FILE *trace;                       /* per tick: last vibrato divisor, gate mirror */
static int wraps;

static void tick_logged(void)
{
    u16 s = log_song;
    bool fetch = DSB(s + SS_PLAYING) && DSW(s + SS_DUR) == 1;
    u16 cur0 = DSW(s + SS_CUR);
    snd_timer_tick();
    fprintf(trace, "%u %u\n", DSW(DS_vib_last_div), DSB(DS_speaker_on));
    if (!fetch) return;
    if (!DSB(s + SS_PLAYING)) { fprintf(ev, "  -- end\n"); return; }
    u16 cur = DSW(s + SS_CUR), base = DSW(s + SS_BASE);
    bool marker = (u16)(cur - cur0) != 4;
    if (marker && DSW(s + SS_REP) == 0 && DSW(s + SS_VIB_PTR) == DSW(s + SS_VIB_TBL)
        && DSW(s + SS_LOOP_PTR) == DSW(s + SS_LOOP_TBL) && cur == (u16)(base + 8))
        wraps++;
    u16 note = DSW(s + SS_NOTE), dur = DSW(s + SS_DUR);
    int rec = (DSW(DS_vib_rec_ptr) - DS_vib_rec) / 10;
    if (note) fprintf(ev, "  note %3d ticks  div %5d  %8.2f Hz  vib rec %d\n", dur, note, (double)PIT_HZ / note, rec);
    else      fprintf(ev, "  rest %3d ticks\n", dur);
}

static void song(const char *name, void (*start)(void), int passes)
{
    load();
    host_set_tick_handler(tick_logged);
    start();
    log_song = DSW(DS_music_cur);
    char wav[64];
    snprintf(wav, sizeof wav, "song_%s.wav", name);
    dump_begin(wav);
    char tr[1024];
    snprintf(tr, sizeof tr, "%s/trace_%s.txt", out_dir, name);
    trace = fopen(tr, "w");
    if (!trace) { fprintf(stderr, "cannot write %s\n", tr); exit(1); }
    ev = tmpfile();
    if (!ev) { fprintf(stderr, "tmpfile failed\n"); exit(1); }
    wraps = 0;
    int ticks = 0;
    while (ticks < 20 * 60 * 73) {
        host_run_tick_now();
        ticks++;
        if (!DSB(log_song + SS_PLAYING) || wraps >= passes) break;
    }
    host_audio_dump_end();
    fprintf(songs_txt, "== %s  %d ticks\n", name, ticks);   /* tick count as srsnd.py counts it */
    rewind(ev);
    char line[256];
    while (fgets(line, sizeof line, ev)) fputs(line, songs_txt);
    fclose(ev);
    fclose(trace);
    printf("song %-12s %6d ticks = %.2f s\n", name, ticks, ticks * 16384.0 / PIT_HZ);
}


static void start_bg(void)     { tune_start(2, 10); }
static void start_7613(void)   { loop_tick(1); }
static void start_7661(void)   { loop_crank(1); }
static void start_76af(void)   { loop_rumble(1); }
static void start_76da(void)   { loop_wrench(1); }
static void start_7573(void)   { loop_spray(1); }
static void start_75c3(void)   { loop_hum100(1); }
static void start_7548(void)   { loop_idle_putt(1); }

/* ---- drive ISR: the tick head of 0000:238c (chain countdown) + its sound part */

static void tick_race(void)
{
    u8 old = DSB(DS_chain_count);
    DSB(DS_chain_count) = (u8)(old - 1);
    if (old == 0) DSB(DS_chain_count) = 3;      /* BIOS chain tick: no sound */
    else snd_race_tick();
}

static void run_ticks(int n) { for (int i = 0; i < n; i++) host_run_tick_now(); }

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s GAME_DIR OUT_DIR\n", argv[0]); return 2; }
    game_dir = argv[1];
    out_dir = argv[2];
    SDL_CreateDirectory(out_dir);
    host_set_realtime(false);

    char path[1024];
    snprintf(path, sizeof path, "%s/songs.txt", out_dir);
    songs_txt = fopen(path, "w");
    if (!songs_txt) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    song("background", start_bg, 1);
    song("loop_7613", start_7613, 3);
    song("loop_7661", start_7661, 3);
    song("loop_76af", start_76af, 3);
    song("loop_76da", start_76da, 3);
    song("loop_7573", start_7573, 3);
    song("loop_75c3", start_75c3, 3);
    song("loop_7548", start_7548, 3);
    fclose(songs_txt);

    /* effects, one file each, the music not playing */
    static const struct { const char *name; void (*fn)(void); } fx[] = {
        { "77ba_click", fx_click }, { "77e1_chirp_hi", fx_chirp_hi }, { "7817_chirp_lo", fx_chirp_lo },
        { "7841_noise", fx_clank }, { "7863_crash", fx_crash }, { "790f_hit", fx_hit },
        { "7973_thud", fx_thud },
    };
    load();
    host_set_tick_handler(snd_timer_tick);
    for (size_t i = 0; i < SDL_arraysize(fx); i++) {
        char wav[64];
        snprintf(wav, sizeof wav, "sfx_%s.wav", fx[i].name);
        dump_begin(wav);
        uint64_t t0 = host_audio_clock();
        fx[i].fn();
        uint64_t t1 = host_audio_clock();
        run_ticks(4);                           /* a little tail for the filters */
        host_audio_dump_end();
        printf("sfx %-14s %.1f ms\n", fx[i].name, (double)(t1 - t0) * 1000.0 / PIT_HZ);
    }

    /* engine table */
    snprintf(path, sizeof path, "%s/engine.csv", out_dir);
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    fprintf(f, "rpm,divisor,hz\n");
    for (int rpm = 0; rpm < 0x1645; rpm += 25) {
        u16 d = engine_divisor((s16)rpm);
        fprintf(f, "%d,%d,%.2f\n", rpm, d, (double)PIT_HZ / (d ? d : 0x10000));
    }
    fprintf(f, "%d,%d,%.2f\n", 0x1644, engine_divisor(0x1644), (double)PIT_HZ / engine_divisor(0x1644));
    fclose(f);

    /* siren + idle engine, 4 s */
    load();
    host_set_tick_handler(tick_race);
    dump_begin("siren.wav");
    DSW(DS_player_rpm) = 0x5DC;
    snd_race_isr_enter();
    siren_set(1);
    run_ticks(4 * 73);
    snd_race_isr_leave();
    run_ticks(8);
    host_audio_dump_end();

    /* engine sweep 1500 -> 5700 RPM, 16 s */
    load();
    host_set_tick_handler(tick_race);
    dump_begin("engine_sweep.wav");
    snd_race_isr_enter();
    for (int i = 0; i < 4 * 73 * 4; i++) {
        DSW(DS_player_rpm) = (u16)(0x5DC + (0x1644 - 0x5DC) * i / (4 * 73 * 4 - 1));
        host_run_tick_now();
    }
    snd_race_isr_leave();
    run_ticks(8);
    host_audio_dump_end();

    /* blocking effects while the drive ISR plays the engine: the ticks inside the effects */
    load();
    host_set_tick_handler(tick_race);
    dump_begin("race_hit.wav");
    DSW(DS_player_rpm) = 3000;
    snd_race_isr_enter();
    run_ticks(40);
    fx_hit();
    run_ticks(40);
    fx_crash();
    run_ticks(40);
    snd_race_isr_leave();
    run_ticks(8);
    host_audio_dump_end();

    printf("written to %s\n", out_dir);
    return 0;
}
