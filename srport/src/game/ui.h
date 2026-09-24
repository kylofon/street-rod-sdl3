#pragma once
/* The 0f38 UI toolkit of SR.EXE, owned by the garage port (game/ui*.c): hot-spot screens ("menus"),
 * message boxes, list boxes, text entry, and the animation-script interpreter. Used by game_flow,
 * garage and race. Specs: video.md §4.9 (listed there without pseudocode), game_flow.md
 * ("Menus", "Message ids"), garage.md §2 / §4.19 / §5.9, platform.md (ui_wait 0000:1417).
 *
 * Conventions (srport/PORTING.md): all state stays in mem[] at the original DGROUP addresses. Near
 * pointers the original passes to the caller's stack (out-parameters, lists and strings built in a
 * stack buffer) are C pointers here; a DGROUP buffer is passed as ds_str(off) / (s16 *)mp(DGROUP, off).
 *
 * Screen records ("menu elements"): 18 bytes at DS:09F4 + 0x12*r:
 *   +0 type, +2 style (colour-scheme entry DS:787E + 5*style), +4 label (DS:239E + x) or message id,
 *   +6 code, +8 x0, +A y0, +C x1, +E y1, +10 next record index (0 = end).
 * Screen n starts at record W[DS:2BAD + 2n]; that first record is the screen header (its +4 is kept in
 * DS:8BAC, type 3 = list without a highlight frame, +10 = first drawn record). Hot spots are the
 * records following the header while their type is > 9 (0x0B active, 0x0C "jump to record +6",
 * 0x0E disabled). Drawing types (0f38:465e): 4/12 box with title, 5 text, 6 centred text, 7/8 edit
 * line, 10 button, 13 list row.
 *
 * UI globals: DS:8B8E stack depth (max 4), DS:8B86[4] the stack, DS:8EFE top screen, DS:8BA6 first
 * hot-spot record of the top screen (DS offset, 0 = none), DS:8BA8 record found by hotspot_at,
 * DS:8BAA its code, DS:8B90/92/94/96 the "no change" rectangle for ui_wait, DS:8B9A first list row,
 * DS:8B9C list row count, DS:8B9E list data, DS:8BA0 selected row, DS:8BA2 edit record, DS:8BA4 edit
 * flag, DS:8B98 "no frame" flag. Bounding box of the last draw: DS:6C56/58/5A/5C. */
#include "types.h"
#include "mem.h"

/* ---- screen records */
#define UI_REC_BASE   0x09F4
#define UI_REC_SIZE   0x12
#define UI_REC(r)     ((u16)(UI_REC_BASE + UI_REC_SIZE * (r)))
#define UI_R_TYPE     0x00
#define UI_R_STYLE    0x02
#define UI_R_LABEL    0x04
#define UI_R_CODE     0x06
#define UI_R_X0       0x08
#define UI_R_Y0       0x0A
#define UI_R_X1       0x0C
#define UI_R_Y1       0x0E
#define UI_R_NEXT     0x10
#define UI_SCREENS    0x2BAD          /* u16[]: first record of screen n */
static inline u16 ui_screen_rec(s16 screen) { return UI_REC(DSS((u16)(UI_SCREENS + 2 * screen))); }

/* ---- screens (0f38:4265 - 4f14) */
void ui_sort_records(s16 first, s16 n);       /* 0f38:4265: sort records first..first+n-1 by x0 (hotspot_at needs it) */
void ui_draw_records(u16 rec);                /* 0f38:465e: draw record rec (DS offset); the chain when DS:07A8 = FFFFh */
void ui_push(s16 screen);                     /* 0f38:4a66: draw screen, make its hot spots current (stack of 4) */
void ui_pop(s16 redraw);                      /* 0f38:4ba1: drop the top screen; redraw != 0: re-push the one below */
void ui_state_reset(void);                    /* 0f38:4bff: clears the stack state (gfx_init) */
void ui_bounds(s16 screen, s16 *x, s16 *y, s16 *w, s16 *h);   /* 0f38:4c1a: rectangle of a screen's records +3 */
/* 0f38:4d10: saves the background under a screen (ui_bounds) into a new arena bitmap (pool). */
FarPtr ui_save_bg(s16 screen, s16 *x, s16 *y, s16 pool);
s16  hotspot_at(s16 x, s16 y);                /* 0f38:4dad: code of the hot spot under (x, y), 0 = none (modules.hotspot_at) */
s16  ui_dialog(s16 screen);                   /* 0f38:4ec1: save background, ui_menu, restore */
s16  ui_menu(s16 screen);                     /* 0f38:4f14: ui_push, ui_wait(32000) until non-zero, ui_pop(1) */
#define ui_run ui_menu                        /* garage.md's name */
/* 0000:0d58 (platform range, not in platform.h): arena_reset, DS:051A/051C/051E = 0, ui_pop(1).
 * PORT: implemented here. */
void hotspots_reset(void);

/* ---- list boxes (0f38:4f4f - 590b), garage.md §4.19. list[0] = count, list[1..] text ids (>= 0:
 * DS:2C02 + id; < 0: string DS offset extra[-id-1]); wear[1..] bytes (0x80 = no wear text) or NULL;
 * xoff[0..] x offsets per row or NULL. Returns 0x3EA for an empty list, else the code that ended it
 * (row clicks only select: list_selected()). */
s16  list_box(s16 screen, s16 sel, const s16 *xoff, s16 *list, const s8 *wear, const u16 *extra);  /* 0f38:4f4f */
s16  list_box_run(s16 screen, s16 sel, const s16 *xoff, s16 *list, const s8 *wear, const u16 *extra); /* 0f38:500e */
u16  wear_text(s16 pct);                      /* 0f38:4fc1: DS offset of "(`new`)" or "(NN% worn)" (DS:07AA) */
s16  list_selected(void);                     /* 0f38:590b: DS:8BA0 */

/* ---- text entry (0f38:5910 - 5e54) */
s16  text_input(s16 x, s16 y, char *buf, s16 maxlen);   /* 0f38:5910: line editor; 13 Enter or a negative code */
s16  prompt_number(s16 x, s16 y, const char *label, u16 *v);   /* 0f38:5d37 (no callers) */
s16  edit_number(u16 *v, char *buf);          /* 0f38:5da7: numeric field of the edit record DS:8BA2; <= 0 */
s16  ui_edit_number(s16 screen, u16 *v);      /* 0f38:5e1e (no callers) */
s16  ui_edit(s16 screen, s16 maxlen, char *buf);   /* 0f38:5e54: text field of a screen; <= 0 */

/* ---- message boxes (0f38:239c - 2adf); message id p: DS:2C01+p box type, DS:2C02+p the lines */
void dialog_msg(const char *s);               /* 0f38:239c: text into DS:291F, ui_dialog(0x1D) */
void fatal_message(const char *s);            /* 0f38:23ce: "... Hit Return when ready" (modules.fatal_message) */
void msg_draw(s16 id);                        /* 0f38:28a4: box picture + text; id < 0: removed at once (sic) */
void msg_draw_at(s16 id, s16 x, s16 y);       /* 0f38:2a62: at (x, y) (< 0: the table's), DS:05DE = 0 */
void msg_erase(void);                         /* 0f38:2adf */

/* ---- animation scripts (0f38:19ec - 1e93): 5 slots of 0x2C bytes at DS:7568, scripts of 6-byte
 * steps at DS:58F0, script start per animation id W[DS:59F8 + 2*id]. */
#define ANIM_SLOTS    0x7568
#define ANIM_SLOT(i)  ((u16)(ANIM_SLOTS + 0x2C * (i)))
void anim_stop(s16 id);                       /* 0f38:19ec */
s16  anim_start(s16 id);                      /* 0f38:1dc9: returns the slot (5 = none free) */
void anim_tick(s16 id);                       /* 0f38:1e41: type-1 tick timer callback (modules.timer_callback) */
s16  anim_active(s16 id);                     /* 0f38:1e72 */
