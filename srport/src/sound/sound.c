/* Sound: music sequencer, music control, loop sounds, driving sounds, tune-up tone
 * (port/spec/sound.md 4.1-4.6). The busy-loop effects are in sfx.c. */
#include "sound/sound.h"
#include "platform/platform.h"
#include "sound/snd_int.h"

/* ---------------------------------------------------------------- helpers */

/* les bx,[s+off] / add [s+off],2 / mov cx,es:[bx]: reads the next song word (offset wraps in the
 * segment, as the far pointer does). */
static u16 song_rd(u16 s)
{
    FarPtr p = ds_far((u16)(s + SS_CUR));
    u16 v = rd16(p.seg, p.off);
    p.off = (u16)(p.off + 2);
    ds_far_wr((u16)(s + SS_CUR), p);
    return v;
}

/* mov bx,[s+1a] / mov ax,0Ah / imul word [bx] / add ax,5826h: vibrato record of the section play. */
static void vib_rec_select(u16 vib_ptr)
{
    DSW(DS_vib_rec_ptr) = (u16)(DS_vib_rec + (u16)(10 * DSW(vib_ptr)));
}

/* PORT: private copy of garage hotspot_find 0000:6827 (memory only): the screen record 74h..85h whose
 * code (+2 of the 18-byte records at DS:09F8) equals `code`, else `code` itself. */
static s16 snd_hotspot_find(s16 code)
{
    for (u16 i = 0; i < 18; i++)
        if (DSS(0x09FA + (0x74 + i) * 0x12) == code) return (s16)(0x74 + i);
    return code;
}

/* Label word (+0) of the jukebox button (code -40) := the menu string offset stored at `lbl`. The
 * quirk stays: with no button -40 on screen the slot is -40 and the write lands at DS:0728. */
static void jukebox_label(u16 lbl)
{
    s16 slot = snd_hotspot_find(-40);
    DSW((u16)(0x09F8 + (u16)(slot * 0x12))) = DSW(lbl);
    click_clear();                      /* 0000:0d78 (platform) */
}

/* ---------------------------------------------------------------- platform hooks */

/* 0000:1111 @1231-1274: initial song structures (the rest of both is 0 in the file). */
void snd_song_defaults(void)
{
    FarPtr bg = far_make(SEG(SEG_SONGS_BG), 0x0000);
    DSB(DS_bg_song + SS_PLAYING) = 0;
    ds_far_wr(DS_bg_song + SS_BASE, bg);
    ds_far_wr(DS_bg_song + SS_SEC_START, bg);
    ds_far_wr(DS_bg_song + SS_CUR, bg);
    DSW(DS_bg_song + SS_LOOP_TBL) = DS_bg_loop_counts;
    DSW(DS_bg_song + SS_LOOP_PTR) = DS_bg_loop_counts;
    DSW(DS_bg_song + SS_VIB_TBL) = DS_bg_vib_index;
    DSW(DS_bg_song + SS_VIB_PTR) = DS_bg_vib_index;
    DSB(DS_fx_song + SS_PLAYING) = 0;
    DSW(DS_fx_song + SS_VIB_TBL) = DS_fx_vib_index;
    DSW(DS_fx_song + SS_VIB_PTR) = DS_fx_vib_index;
}

/* 0000:1fe2 @2028 */
void snd_timer_gate_check(void)
{
    if (DSB(DS_sound_on) == 0 && DSB(DS_speaker_on) != DSB(DS_sound_on)) spk_gate_off();
}

/* 0000:1fe2 @20b8-2294: the music sequencer (sound.md 4.3). */
void snd_music_tick(void)
{
    u16 s = DSW(DS_music_cur);
    if (!DSB(s + SS_PLAYING)) return;
    if (--DSW(s + SS_DUR) != 0) {
        /* 20c7: vibrato step of the sounding note */
        if (DSW(DS_vib_enabled) == 0) return;
        u16 note = DSW(s + SS_NOTE);
        if (note == 0) return;
        if (--DSW(DS_vib_countdown) != 0) return;
        u16 rec = DSW(DS_vib_rec_ptr);
        s16 period = DSS(rec + VR_PERIOD);
        /* PORT: the original's IDIV would raise a divide error here; unreachable (a zero record has
         * step 0, so the countdown wraps before it fires again, sound.md 5.2). */
        if (period == 0 || (period == -1 && DSS(rec + VR_PHASE) == -32768)) return;
        s16 r = (s16)(DSS(rec + VR_PHASE) % period);          /* cwd / idiv: remainder */
        DSW(rec + VR_PHASE) = (u16)(r + 1);
        u16 w_off = (u16)((u16)(DSW(rec + VR_ROW) << 4) + (u16)((u16)r << 1));
        s16 w = DSS((u16)(DS_vib_wave + w_off));               /* no bound check */
        u16 div = (u16)((u16)((s32)w * DSS(DS_vib_amount)) + note);   /* imul: low word */
        DSW(DS_vib_last_div) = div;
        pit2_div(div);
        DSW(DS_vib_countdown) = DSW(rec + VR_STEP);
        return;
    }
    for (;;) {                                                /* 21f1: next event(s) */
        u16 dur = song_rd(s);
        DSW(s + SS_DUR) = dur;
        if (dur != 0) break;
        if (song_rd(s) == 0) {                                /* 213b: end of song */
            spk_gate_off();
            DSB(s + SS_PLAYING) = 0;
            DSB(s + SS_REGATE) = 1;
            FarPtr base = ds_far(s + SS_BASE);
            ds_far_wr(s + SS_CUR, base);
            ds_far_wr(s + SS_SEC_START, base);
            return;
        }
        /* 217c: section marker */
        DSW(s + SS_VIB_PTR) = (u16)(DSW(s + SS_VIB_PTR) + 2);
        u16 loop_ptr = DSW(s + SS_LOOP_PTR);
        DSW(s + SS_REP) = (u16)(DSW(s + SS_REP) + 1);
        if (DSS(loop_ptr) >= DSS(s + SS_REP)) {
            ds_far_wr(s + SS_CUR, ds_far(s + SS_SEC_START));  /* 21d1: play the section again */
        } else {
            DSW(s + SS_LOOP_PTR) = (u16)(loop_ptr + 2);
            if (DSS(DSW(s + SS_LOOP_PTR)) < 0) {              /* end of the table: whole song again */
                ds_far_wr(s + SS_CUR, far_add(ds_far(s + SS_BASE), 4));
                DSW(s + SS_LOOP_PTR) = DSW(s + SS_LOOP_TBL);
                DSW(s + SS_VIB_PTR) = DSW(s + SS_VIB_TBL);
            }
            ds_far_wr(s + SS_SEC_START, ds_far(s + SS_CUR));
            DSW(s + SS_REP) = 0;
        }
        vib_rec_select(DSW(s + SS_VIB_PTR));
    }
    u16 note = song_rd(s);
    DSW(s + SS_NOTE) = note;
    if (note == 0) {                                          /* 2225: rest (divisor kept) */
        DSB(s + SS_REGATE) = 1;
        spk_gate_off();
        return;
    }
    if (DSW(DS_vib_enabled)) {
        u16 rec = DSW(DS_vib_rec_ptr);
        DSW(DS_vib_countdown) = DSW(rec + VR_STEP);
        /* mul word [note] / sub dx,dx / div 1000: only the low word of depth*note counts */
        DSW(DS_vib_amount) = (u16)((u16)(DSW(rec + VR_DEPTH) * note) / 1000);
    }
    pit2_div(note);
    if (DSB(DS_sound_on) != 0 &&
        (DSB(DS_speaker_on) != DSB(DS_sound_on) || DSB(s + SS_REGATE) != 0)) {
        spk_gate_on();
        DSB(s + SS_REGATE) = 0;
    }
}

void snd_timer_tick(void)
{
    snd_timer_gate_check();
    snd_music_tick();
}

/* 0000:238c @2404-2442 (the note is refreshed on 1 tick of 4). The SHR by (t & 0FFh) + 2 uses the
 * 286+ / DOSBox count masking (count & 31; 16..31 shift everything out). */
u16 engine_divisor(s16 rpm)
{
    s32 sq = (s32)rpm * rpm;                                  /* _aFlmul */
    u16 t = (u16)(sq >> 10);                                  /* _aFlshr, low word */
    unsigned n = (u8)((u8)t + 2) & 31;
    u16 x = n >= 16 ? 0 : (u16)(t >> n);
    x >>= 3;
    t = (u16)(t + x);
    u16 bonus = rpm == 0x1644 ? 0x2710 : 0;                   /* rev limiter: jumps up */
    return (u16)(0xC350 - (u16)(bonus + t));                  /* sub ax,C350h / neg ax */
}

/* 0000:238c @245e-248e: engine note while the animation player 0f38:8e48 runs. */
u16 anim_engine_divisor(void)
{
    s16 d = (s16)(DSW(DS_car_x) - DSW(DS_anim_engine_ref));
    u16 a = d < 0 ? (u16)-d : (u16)d;
    u16 t = (u16)((u32)(u16)(a >> 1) * a);                    /* mul: low word */
    if (t > 20000) t = 20000;                                 /* unsigned compare */
    return (u16)(60000 - t);                                  /* 19.9 .. 29.8 Hz */
}

/* 0000:238c @23d3-2512: sound part of the drive ISR on the non-chain ticks. */
void snd_race_tick(void)
{
    if (DSB(DS_sound_on) == 0) {                              /* 24f6 */
        if (DSB(DS_speaker_on) != DSB(DS_sound_on)) spk_gate_off();
        return;
    }
    if (DSB(DS_speaker_on) != DSB(DS_sound_on)) spk_gate_on(); /* keeps the gate on */
    if (DSB(DS_chain_count) & 1) {                            /* 1 of 4 ticks: engine */
        if (DSW(DS_anim_mode) == 0) pit2_div(engine_divisor(DSS(DS_player_rpm)));
        else if (DSW(DS_anim_engine_snd) != 0) pit2_div(anim_engine_divisor());
        return;
    }
    DSB(DS_squeal_phase) ^= 1;                                /* 2490: the other two sound ticks */
    if (DSW(DS_siren_on)) {
        u16 old = DSW(DS_siren_count);
        DSW(DS_siren_count) = (u16)(old + 1);
        pit2_div((s16)old < 10 ? 0x054B : 0x0712);           /* 880.6 / 659.2 Hz */
        if (DSS(DS_siren_count) > 20) DSW(DS_siren_count) = 0;
    } else if ((DSW(DS_squeal_on) || DSW(DS_offroad)) && DSB(DS_squeal_phase)) {
        pit2_div(0x0095);                                     /* 8008 Hz blip for one tick */
    }
}

/* 0000:1aad @1b09-1b26 */
void snd_race_isr_enter(void)
{
    pit2_div(0x001E);                                         /* 39.8 kHz: inaudible until the engine */
    if (DSB(DS_sound_on)) spk_gate_on();
}

/* 0000:1b27 @1b33-1b4a */
void snd_race_isr_leave(void)
{
    DSW(DS_siren_on) = 0;
    DSW(DS_squeal_on) = 0;
    DSW(DS_offroad) = 0;
    spk_gate_off();
}

/* ---------------------------------------------------------------- music control (4.1) */

/* 0000:1a05 */
void tune_start(int word_off, int delay)
{
    u16 s = DSW(DS_music_cur);
    if (DSW(DS_music_enabled) == 0) { DSB(s + SS_PLAYING) = 0; return; }
    DSB(s + SS_PLAYING) = 0;
    spk_gate_off();
    DSW(s + SS_REP) = 0;
    DSB(s + SS_REGATE) = 1;
    FarPtr p = far_add(ds_far(s + SS_BASE), (u16)(word_off << 1));
    ds_far_wr(s + SS_CUR, p);
    ds_far_wr(s + SS_SEC_START, p);
    DSW(s + SS_DUR) = (u16)(delay + 1);
    DSW(s + SS_VIB_PTR) = DSW(s + SS_VIB_TBL);
    vib_rec_select(DSW(s + SS_VIB_PTR));
    DSW(DS_vib_enabled) = 1;
    DSB(s + SS_PLAYING) = 1;
    /* quirk: SS_LOOP_PTR is not reset (only song_select sets it, for loop sounds) */
}

/* 0000:1a7c */
void tune_rewind(void)
{
    u16 s = DSW(DS_music_cur);
    DSB(s + SS_PLAYING) = 0;
    spk_gate_off();
    DSB(s + SS_REGATE) = 1;
    FarPtr base = ds_far(s + SS_BASE);
    ds_far_wr(s + SS_CUR, base);
    ds_far_wr(s + SS_SEC_START, base);
}

/* 0000:0d99 */
void music_stop(void)
{
    u16 s = DSW(DS_music_cur);
    if (!DSB(s + SS_PLAYING)) return;
    DSB(s + SS_PLAYING) = 0;
    spk_gate_off();
    DSB(s + SS_REGATE) = 1;
    jukebox_label(DS_lbl_tunes);                              /* "Catch some tunes" */
}

/* 0000:0dd8 */
void music_resume(void)
{
    if (DSW(DS_music_enabled) == 0) return;
    u16 s = DSW(DS_music_cur);
    if (DSB(s + SS_PLAYING)) return;
    FarPtr a = ds_far(s + SS_SEC_START), b = ds_far(s + SS_BASE);
    if (a.off == b.off && a.seg == b.seg) return;            /* never started (or ended) */
    DSB(s + SS_PLAYING) = 1;
    DSB(s + SS_REGATE) = 1;
    jukebox_label(DS_lbl_squelch);                            /* "Squelch it !" */
}

/* inline in 0000:503f (button -40), 0000:1417 (button -40 while music_mode == 1), 0f38:0b51 ('M') */
void music_toggle(void)
{
    if (DSW(DS_music_enabled) == 0) { DSW(DS_music_enabled) = 1; music_resume(); }
    else                            { music_stop(); DSW(DS_music_enabled) = 0; }
}

/* 0f38:7001 */
void music_mute(int mute)
{
    if (mute) {
        if (DSW(DS_music_enabled) == 0) { DSW(DS_music_muted) = 0; return; }
        music_stop();
        DSW(DS_music_enabled) = 0;
        DSW(DS_music_muted) = 1;
    } else {
        if (DSW(DS_music_muted) == 0) return;
        DSW(DS_music_enabled) = 1;
        music_resume();
        DSW(DS_music_muted) = 0;
    }
}

/* 0f38:6fec (no callers): vibrato off, gate off without the DS_speaker_on mirror, rewind. */
void music_off_rewind(void)
{
    DSW(DS_vib_enabled) = 0;
    host_port61((u8)(host_port61_get() & 0xFC));
    tune_rewind();
}

/* ---------------------------------------------------------------- loop sounds (4.2) */

/* 0f38:6f0a */
void song_select(u16 off, u16 seg, u16 loop_tbl)
{
    if (off == DSW(DS_bg_song + SS_BASE) && seg == DSW(DS_bg_song + SS_BASE + 2)) {
        if (DSS(DS_music_enabled_saved) != -1) {
            DSW(DS_music_enabled) = DSW(DS_music_enabled_saved);
            DSW(DS_music_enabled_saved) = 0xFFFF;
        }
        if (DSW(DS_music_enabled) == 0) spk_gate_off();
        DSW(DS_music_mode) = 1;
        DSW(DS_music_cur) = DS_bg_song;                        /* goes on where it was */
        vib_rec_select(DSW(DS_bg_song + SS_VIB_PTR));
        DSW(DS_vib_enabled) = 1;
        /* DS_vib_countdown / DS_vib_amount keep the loop sound's values until the next note */
    } else {
        if (DSS(DS_music_enabled_saved) == -1) {              /* plays even with the music off */
            DSW(DS_music_enabled_saved) = DSW(DS_music_enabled);
            DSW(DS_music_enabled) = 1;
        }
        DSW(DS_music_mode) = 2;
        FarPtr p = far_make(seg, off);
        ds_far_wr(DS_fx_song + SS_BASE, p);
        ds_far_wr(DS_fx_song + SS_SEC_START, p);
        ds_far_wr(DS_fx_song + SS_CUR, p);
        DSW(DS_fx_song + SS_LOOP_TBL) = loop_tbl;
        DSW(DS_fx_song + SS_LOOP_PTR) = loop_tbl;
        DSW(DS_music_cur) = DS_fx_song;
        tune_start(2, 0);                                      /* first event on the next tick */
    }
}

/* 0f38:6fbd: vibrato record 0 */
void vib_set(int on, int row, int period, int depth, int step)
{
    if (on) {
        DSW(DS_vib_rec + VR_PHASE) = 0;
        DSW(DS_vib_rec + VR_ROW) = (u16)row;
        DSW(DS_vib_rec + VR_DEPTH) = (u16)depth;
        DSW(DS_vib_rec + VR_PERIOD) = (u16)period;
        DSW(DS_vib_rec + VR_STEP) = (u16)step;
    }
    DSW(DS_vib_enabled) = (u16)on;
}

static void loop_off(void)
{
    song_select(0x0000, SEG(SEG_SONGS_BG), DS_bg_loop_counts);
}

static void loop_on(u16 off)
{
    song_select(off, SEG(SEG_SONGS_FX), DS_fx_loop_counts);
}

void loop_idle_putt(int on) { if (on) loop_on(0x00C0); else loop_off(); }                     /* 0f38:7548 */
void loop_spray(int on)     { if (on) { loop_on(0x00A0); vib_set(1, 0, 8, 0x0A, 1); } else loop_off(); } /* 0f38:7573 */
void loop_hum100(int on)    { if (on) { loop_on(0x00B0); vib_set(1, 0, 8, 0x05, 1); } else loop_off(); } /* 0f38:75c3 */
void loop_tick(int on)      { if (on) { loop_on(0x0000); vib_set(1, 1, 8, 0x1E, 1); } else loop_off(); } /* 0f38:7613 */
void loop_crank(int on)     { if (on) { loop_on(0x002C); vib_set(1, 1, 8, 0x14, 1); } else loop_off(); } /* 0f38:7661 */
void loop_rumble(int on)    { if (on) loop_on(0x0050); else loop_off(); }                     /* 0f38:76af */
void loop_wrench(int on)    { if (on) { loop_on(0x007C); vib_set(1, 1, 8, 0x0E, 1); } else loop_off(); } /* 0f38:76da */

/* ---------------------------------------------------------------- driving flags */

void siren_set(int on)  { DSW(DS_siren_on) = on != 0; }   /* 0f38:7728 */
void squeal_set(int on) { DSW(DS_squeal_on) = on != 0; }  /* 0f38:7741 */

/* ---------------------------------------------------------------- tune-up screen 0000:7fb1 (4.4) */

void ignition_tune_begin(void)                            /* @801a */
{
    music_stop();
    DSW(DS_music_mode) = 2;                               /* blocks the jukebox button in 1417 */
    if (DSB(DS_sound_on)) spk_gate_on();
}

void ignition_tune_gate(void)                             /* @803f, @80f0 (in the key poll loop) */
{
    if (DSB(DS_sound_on) && DSB(DS_sound_on) != DSB(DS_speaker_on)) spk_gate_on();
}

void ignition_tune_tone(s8 setting)                       /* @805c: cbw / abs / shl 10 */
{
    s16 v = setting;
    u16 a = (u16)(v < 0 ? -v : v);
    pit2_div((u16)((u16)(a << 10) + 0x7530));             /* 39.8 .. 31.2 Hz */
}

void ignition_tune_end(void)                              /* @8151 */
{
    spk_gate_off();
    DSW(DS_music_mode) = 1;
    music_resume();
}
