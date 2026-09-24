#pragma once
/* Sound: the PC-speaker driver of SR.EXE (port/spec/sound.md).
 *
 * Implements sound.md
 *   4.1  music start / stop / resume / toggle / mute / rewind      (0000:0d99 0dd8 1a05 1a7c, 0f38:7001)
 *   4.2  loop sounds and vibrato                                   (0f38:6f0a 6fbd 6fec 7548-76da)
 *   4.3  the sound part of the timer ISR 0000:1fe2: gate check + music sequencer (20b8-2295)
 *   4.4  the sound part of the tune-up screen 0000:7fb1
 *   4.5  the sound part of the drive ISR switch 0000:1aad / 1b27
 *   4.6  the sound part of the drive ISR 0000:238c: engine (0000:2404), animation engine, siren,
 *        squeal / off-road blip
 *   4.7  initialisation, delay and the busy-loop effects           (0f38:703e-7998, 2196:0002)
 *   5    song / vibrato data read from mem[] at their original addresses (2e6b:0000, 2f93:0000...)
 *   6    hardware through host_pit2_divisor / host_port61 / host_busy_wait_ns (host.h)
 *   7    effect timing: the SND_* constants below
 * Not ported (dead code, no callers): the PWM effects 0f38:7292-73ed, 775a, 778a and the 3-voice
 * pulse player 2196:001d.
 *
 * All state lives in mem[] (DGROUP globals DS_sound_on, DS_music_*, song structures DS_bg_song /
 * DS_fx_song, vibrato records ...), so the module has no C state except the random-number hook.
 *
 * Who calls what
 * --------------
 * platform (startup, ISRs):
 *   snd_song_defaults()   from platform_install 0000:1111 (its lines 1231-1274), before the timer
 *   snd_init()            0f38:7101, from main 0000:07d3
 *   snd_timer_tick()      from timer_isr 0000:1fe2 = snd_timer_gate_check() [at 2028, right after the
 *                         BIOS chain] ... demo/cursor-glide part ... snd_music_tick() [at 20b8]. Call
 *                         the two halves separately to keep the demo step between them exact.
 *   snd_race_tick()       from race_isr 0000:238c on the three non-chain ticks (DS_chain_count's old
 *                         value != 0), after the EOI; it reads the decremented DS_chain_count itself
 *   snd_race_isr_enter()  sound part of race_isr_enter 0000:1aad (at its end, 1b09-1b26)
 *   snd_race_isr_leave()  sound part of race_isr_leave 0000:1b27 (after the vector swap: clears the
 *                         siren / squeal / off-road flags, speaker off)
 *   spk_off()             platform_exit 0000:0fea's "speaker off" (0f38:713d is the same code)
 *   snd_set_rnd(rnd)      once at start-up: the platform's rnd 0f38:5eb6 (effects consume it)
 *   music_toggle()        key 'M' in 0f38:0b51
 * game_flow / garage / race:
 *   tune_start(2, 10)     game start 0000:39c0: the background tune begins 11 ticks later
 *   music_toggle()        jukebox button -40 in 0000:503f; in ui_wait 0000:1417 only while
 *                         DSW(DS_music_mode) == 1
 *   music_mute(1 / 0)     0000:8ea8 (if DS:51EC == 0), 0f38:be93 ... (sound.md section 2, callers)
 *   loop_*(1 / 0)         loop sounds (they replace the background tune while on)
 *   fx_*()                blocking effects (return after 4 ms .. 0.3 s)
 *   siren_set / squeal_set, DSW(DS_squeal_on) / DSW(DS_offroad) directly as the original
 *   ignition_tune_*()     the tune-up screen 0000:7fb1
 *   engine_divisor / anim_engine_divisor: the two engine-note formulas (used by snd_race_tick)
 */
#include "types.h"

/* ---- Effect timing (sound.md 7, DOSBox reference). The original calibrates delay() at start-up
 * (0f38:703e); the port uses these fixed values instead. */
#define SND_DELAY_SCALE        3432u    /* value the port stores in DS_delay_scale (58E2h) */
#define SND_DELAY_UNIT_NS      4470u    /* one delay(1) unit at SND_DELAY_SCALE: 4.47 us */
#define SND_TONE_OVERHEAD_NS  20000u    /* one fx_tone call besides its delay (_aFldiv, outs, calls) */
#define SND_NOISE_OVERHEAD_NS 25000u    /* one fx_noise step besides its delay (the rnd call) */

/* ---- Platform hooks ---- */
void snd_song_defaults(void);           /* 0000:1111 @1231: bg_song / fx_song initial pointers */
void snd_init(void);                    /* 0f38:7101 */
void snd_timer_gate_check(void);        /* 0000:1fe2 @2028: gate off when sound is switched off */
void snd_music_tick(void);              /* 0000:1fe2 @20b8: music sequencer, one tick */
void snd_timer_tick(void);              /* both of the above, in order */
void snd_race_tick(void);               /* 0000:238c @23d3: engine / siren / squeal, one tick */
void snd_race_isr_enter(void);          /* 0000:1aad @1b09: ch2 divisor 1Eh, gate on if sound on */
void snd_race_isr_leave(void);          /* 0000:1b27 @1b33: siren/squeal/off-road off, gate off */
/* Random numbers for the effects: the platform's rnd (0f38:5eb6: n > 0 -> 0..n-1, n <= 0 reseeds).
 * Until it is set, a PORT stand-in LCG is used. */
void snd_set_rnd(int (*rnd)(int n));

/* ---- Music (the background tune DS_bg_song, data 2e6b:0000, or a loop sound DS_fx_song) ---- */
void tune_start(int word_off, int delay);   /* 0000:1a05: current song from base + 2*word_off after
                                               delay+1 ticks (does nothing but stop if music is off) */
void tune_rewind(void);                     /* 0000:1a7c (only from the dead 0f38:6fec) */
void music_stop(void);                      /* 0000:0d99: relabels the jukebox button -40 */
void music_resume(void);                    /* 0000:0dd8: relabels the jukebox button -40 */
void music_toggle(void);                    /* inline in 0000:503f, 0000:1417, 0f38:0b51 */
void music_mute(int mute);                  /* 0f38:7001: 1 stop and remember, 0 undo */
void music_off_rewind(void);                /* 0f38:6fec (no callers) */
void song_select(u16 off, u16 seg, u16 loop_tbl);   /* 0f38:6f0a; seg is a mem[] segment (SEG()) */
void vib_set(int on, int row, int period, int depth, int step);   /* 0f38:6fbd */

/* Loop sounds (on = 1 starts, 0 returns to the background tune). Names are guesses (sound.md 8). */
void loop_idle_putt(int on);                /* 0f38:7548  2f93:00C0 */
void loop_spray(int on);                    /* 0f38:7573  2f93:00A0 */
void loop_hum100(int on);                   /* 0f38:75c3  2f93:00B0 (no callers) */
void loop_tick(int on);                     /* 0f38:7613  2f93:0000 */
void loop_crank(int on);                    /* 0f38:7661  2f93:002C */
void loop_rumble(int on);                   /* 0f38:76af  2f93:0050 (no callers) */
void loop_wrench(int on);                   /* 0f38:76da  2f93:007C */

/* ---- Driving ---- */
void siren_set(int on);                     /* 0f38:7728: DS_siren_on = on != 0 */
void squeal_set(int on);                    /* 0f38:7741: DS_squeal_on = on != 0 */
u16  engine_divisor(s16 rpm);               /* 0000:238c @2404: PIT divisor of the engine note */
u16  anim_engine_divisor(void);             /* 0000:238c @245e: note while the animation player
                                               0f38:8e48 runs (DS_car_x - DS_anim_engine_ref) */

/* ---- Tune-up screen 0000:7fb1 (sound part; the garage owns the loop around it) ---- */
void ignition_tune_begin(void);             /* @801a: music_stop, music_mode = 2, gate on */
void ignition_tune_gate(void);              /* @803f/@80f0: gate back on if sound was re-enabled */
void ignition_tune_tone(s8 setting);        /* @805c: divisor 7530h + |setting| << 10 */
void ignition_tune_end(void);               /* @8151: gate off, music_mode = 1, music_resume */

/* ---- Speaker primitives and blocking effects (all effects return at once if sound is off) ---- */
void spk_set_hz(s32 hz);                    /* 0f38:70d5: ch2 divisor 1234DCh / hz */
void spk_on(void);                          /* 0f38:7122: 25 kHz, gate + data on */
void spk_off(void);                         /* 0f38:713d: gate + data off */
int  snd_calibrate_delay(int n);            /* 0f38:703e (PORT: returns SND_DELAY_SCALE) */
void delay(u16 n);                          /* 2196:0002: busy wait, n units of DS_delay_scale */
void fx_click(void);                        /* 0f38:77ba: UI button press, ~10 ms */
void fx_chirp_hi(void);                     /* 0f38:77e1: ~4 ms */
void fx_chirp_lo(void);                     /* 0f38:7817: ~4 ms */
void fx_clank(void);                        /* 0f38:7841: noise, ~30 ms */
void fx_crash(void);                        /* 0f38:7863: wreck, ~270 ms */
void fx_hit(void);                          /* 0f38:790f: racing collision, ~30 ms */
void fx_thud(void);                         /* 0f38:7973: low sweep, ~49 ms */
