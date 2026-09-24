#pragma once
/* Private helpers of the sound module (not for other subsystems: use sound.h). */
#include "host.h"
#include "mem.h"
#include "symbols.h"

/* SongState (28 bytes) at DS_bg_song / DS_fx_song, sound.md 3. */
#define SS_NOTE      0x00   /* u16 PIT divisor of the sounding note, 0 = rest */
#define SS_DUR       0x02   /* u16 ticks left of the current event */
#define SS_REP       0x04   /* s16 plays of the current section so far */
#define SS_PLAYING   0x06   /* u8 */
#define SS_REGATE    0x07   /* u8 1 = switch the gate on at the next note */
#define SS_BASE      0x08   /* far song start (events from base + 4) */
#define SS_SEC_START 0x0C   /* far start of the current section */
#define SS_CUR       0x10   /* far read pointer */
#define SS_LOOP_TBL  0x14   /* u16 near ptr: s16 extra plays per section, < 0 = restart */
#define SS_LOOP_PTR  0x16   /* u16 current entry */
#define SS_VIB_TBL   0x18   /* u16 near ptr: u16 vibrato record index per section play */
#define SS_VIB_PTR   0x1A   /* u16 current entry */

/* Vibrato record (10 bytes) at DS_vib_rec + 10*i. */
#define VR_PHASE  0
#define VR_ROW    2
#define VR_DEPTH  4
#define VR_PERIOD 6
#define VR_STEP   8

#define SEG_SONGS_BG  0x2E6B    /* file segments of the song data (use SEG()) */
#define SEG_SONGS_FX  0x2F93

/* The two speaker-gate idioms of the original (in al,61h / or al,3 | and ax,0FCh / out 61h,al,
 * then the DS_speaker_on mirror). */
static inline void spk_gate_on(void)
{
    host_port61((u8)(host_port61_get() | 3));
    DSB(DS_speaker_on) = 1;
}
static inline void spk_gate_off(void)
{
    host_port61((u8)(host_port61_get() & 0xFC));
    DSB(DS_speaker_on) = 0;
}
/* out 42h, lo / out 42h, hi (channel 2 is in mode 3 from 43h = B6h). */
static inline void pit2_div(u16 d) { host_pit2_divisor(d); }

int snd_rnd(int n);             /* the platform's rnd 0f38:5eb6 through snd_set_rnd */
