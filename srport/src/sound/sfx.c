/* Sound: initialisation, delay() and the busy-loop effects 0f38:703e-7998 (port/spec/sound.md 4.7).
 *
 * The effects block the caller, as in the original. Their time runs on the host's emulated
 * timeline (host_busy_wait_ns): timer ticks that fall inside an effect run at their place, so the
 * music sequencer or the drive ISR change the speaker in the middle of an effect as they did on the
 * PC. Durations use the SND_* constants of sound.h (DOSBox reference, sound.md 7). */
#include "sound/sound.h"
#include "sound/snd_int.h"

/* ---------------------------------------------------------------- random numbers */

static int (*rnd_hook)(int n);

void snd_set_rnd(int (*rnd)(int n)) { rnd_hook = rnd; }

/* PORT: stand-in until the platform's rnd 0f38:5eb6 is registered with snd_set_rnd (then the effects
 * share the game's Wichmann-Hill stream in mem[] as in the original). */
static int rnd_standin(int n)
{
    static u32 x = 1;
    if (n <= 0) return -1;
    x = x * 1103515245u + 12345u;
    return (int)((x >> 16) & 0x7FFF) % n;
}

int snd_rnd(int n) { return rnd_hook ? rnd_hook(n) : rnd_standin(n); }

/* ---------------------------------------------------------------- init, delay, speaker */

/* 0f38:703e. The original counts delay(n) calls during one BIOS tick (0:046C) with the scale at 100h
 * and keeps 3/4 of the count above 350h.
 * PORT: no measurement; returns SND_DELAY_SCALE, the constant the port's delay() timing is
 * calibrated for (sound.md 6/7). */
int snd_calibrate_delay(int n)
{
    (void)n;
    DSW(DS_delay_scale) = 0x100;
    return (int)SND_DELAY_SCALE;
}

/* 2196:0002: mul by the scale; inner LOOP count = bits 8-23 (0 = 65536), repeated bits 24-31 + 1
 * times. Each LOOP costs SND_DELAY_UNIT_NS * 256 / SND_DELAY_SCALE. */
void delay(u16 n)
{
    u32 p = (u32)n * DSW(DS_delay_scale);
    u32 inner = (u16)(p >> 8);
    u32 outer = (u8)(p >> 24);
    uint64_t loops = (uint64_t)(outer + 1) * (inner ? inner : 0x10000u);
    host_busy_wait_ns(loops * SND_DELAY_UNIT_NS * 256 / SND_DELAY_SCALE);
}

/* 0f38:70d5: _aFldiv(1234DCh, hz), low word to channel 2 */
void spk_set_hz(s32 hz)
{
    if (hz == 0) div_error();                               /* the runtime's divide error */
    pit2_div((u16)(0x1234DC / hz));
}

/* 0f38:7101 (from main 0000:07d3) */
void snd_init(void)
{
    DSW(DS_delay_scale) = (u16)snd_calibrate_delay(10);
    /* out 43h, B6h: channel 2 lo/hi mode 3 - implied by the host model */
    spk_set_hz(25000);
}

void spk_on(void)  { spk_set_hz(25000); spk_gate_on(); }    /* 0f38:7122 */
void spk_off(void) { spk_gate_off(); }                      /* 0f38:713d */

/* ---------------------------------------------------------------- building blocks (near) */

/* 0f38:714c */
static void fx_tone(s32 hz, u16 d)
{
    spk_set_hz(hz);
    host_busy_wait_ns(SND_TONE_OVERHEAD_NS);                /* PORT: uncalibrated call overhead */
    delay(d);
}

/* 0f38:7169: n random tones lo + rnd(hi - lo), rnd called once per tone */
static void fx_burst(s16 lo, s16 hi, u16 d, s16 n)
{
    s16 range = (s16)(hi - lo);
    if (n <= 0) return;
    do fx_tone((s16)(snd_rnd(range) + lo), d);
    while (--n);
}

/* 0f38:71b2 */
static void fx_sweep_down(s16 from, s16 to, s16 step, u16 d)
{
    for (s16 f = from; f >= to; f = (s16)(f - step)) fx_tone(f, d);
}

/* 0f38:71e4: n times up c-dev -> c+dev-step, then down c+dev -> c-dev+step */
static void fx_warble(s16 c, s16 dev, s16 step, u16 d, s16 n)
{
    if (n <= 0) return;
    s16 span = (s16)(dev << 1);
    do {
        if (span > 0) {
            s16 f = (s16)(c - dev), i = 0;
            do { fx_tone(f, d); f = (s16)(f + step); i = (s16)(i + step); } while (span > i);
        }
        if (span > 0) {
            s16 f = (s16)(c + dev), i = 0;
            do { fx_tone(f, d); f = (s16)(f - step); i = (s16)(i + step); } while (span > i);
        }
    } while (--n);
}

/* 0f38:7464: bit-banged noise: gate off, then n x (toggle bit 1, delay(rnd(range) | ormask)).
 * DS_speaker_on is not touched by the toggles. */
static void fx_noise(s16 range, s16 ormask, s16 n)
{
    spk_off();
    if (n <= 0) return;
    do {
        host_port61((u8)(host_port61_get() ^ 2));
        host_busy_wait_ns(SND_NOISE_OVERHEAD_NS);           /* PORT: uncalibrated rnd call */
        delay((u16)(snd_rnd(range) | ormask));
    } while (--n);
}

/* 0f38:74b0: noise blocks with the range falling from a (exclusive of b) */
static void fx_noise_down(s16 a, s16 b, s16 sub, s16 step, s16 n)
{
    if (a <= b) return;
    s16 x = a;
    do {
        fx_noise(a, (s16)(a - sub), n);
        a = (s16)(a - step);
        x = (s16)(x - step);
    } while (x > b);
}

/* 0f38:74fc: rising version */
static void fx_noise_up(s16 a, s16 b, s16 sub, s16 step, s16 n)
{
    if (a >= b) return;
    s16 x = a;
    do {
        fx_noise(a, (s16)(a - sub), n);
        a = (s16)(a + step);
        x = (s16)(x + step);
    } while (x < b);
}

/* ---------------------------------------------------------------- effects (far, public) */

/* 0f38:77ba: UI button press */
void fx_click(void)
{
    if (DSB(DS_sound_on) == 0) return;
    spk_on();
    fx_warble(3000, 10, 10, 60, 9);
    spk_off();
}

/* 0f38:77e1 */
void fx_chirp_hi(void)
{
    if (DSB(DS_sound_on) == 0) return;
    spk_on();
    fx_warble((s16)(snd_rnd(100) + 0x1838), 40, 10, 1, 10);
    spk_off();
}

/* 0f38:7817 */
void fx_chirp_lo(void)
{
    if (DSB(DS_sound_on) == 0) return;
    spk_on();
    fx_warble(0x1450, 40, 10, 1, 10);
    spk_off();
}

/* 0f38:7841 */
void fx_clank(void)
{
    if (DSB(DS_sound_on) == 0) return;
    spk_on();
    fx_noise(200, 10, 60);
    spk_off();
}

/* 0f38:7863: wreck. rnd order as the arguments are evaluated: the rnd argument before the call. */
void fx_crash(void)
{
    if (DSB(DS_sound_on) == 0) return;
    for (int i = 3; i; i--) {
        spk_on();
        fx_burst(3000, 8000, 60, 10);
        fx_noise_down(200, (s16)(snd_rnd(30) + 18), 15, 5, 2);
        fx_noise((s16)(snd_rnd(300) + 40), 2, 30);
        fx_noise_up(16, (s16)(snd_rnd(100) + 50), 15, 5, 4);
    }
    spk_off();
}

/* 0f38:790f: collision while racing */
void fx_hit(void)
{
    if (DSB(DS_sound_on) == 0) return;
    spk_on();
    fx_burst(2000, 8000, 100, 10);
    fx_noise(120, 2, 30);
    spk_on();
    fx_burst(4000, 9000, 100, 10);
    fx_noise(220, 2, 20);
    spk_off();
}

/* 0f38:7973 */
void fx_thud(void)
{
    if (DSB(DS_sound_on) == 0) return;
    spk_on();
    fx_sweep_down(130, 90, 4, 1000);
    spk_off();
}
