/* Picture libraries LIB1/LIB2, HOT_DATA, hall_dat and the random generator — platform.md §2.5, §2.6,
 * §4.10, §4.11, §5.3 - §5.6 (file layout: FORMATS.md).
 *
 * Picture directory: segment 389b, 48 bytes per picture (§5.3). One library handle is open at a time
 * (lib_select); packed pictures are loaded into far memory, into the shared buffer DS:7564, or parked
 * in one plane of the off-screen EGA memory (A000:7D00+, through the VGA register model).
 * Message boxes (0f38:23ce) go through modules.fatal_message. The floppy-disk prompts cannot trigger in
 * the port (dos_getdrive reports C:), but their logic is kept. */
#include "platform/platform.h"
#include "platform/video.h"
#include "platform/vga.h"
#include "modules.h"

#include <string.h>
#include <time.h>

/* Picture directory entry fields (§5.3) */
#define PE_W       0x00
#define PE_H       0x02
#define PE_MASKED  0x06
#define PE_TOKENS  0x07
#define PE_OFFSET  0x18    /* u32 from the end of the directory */
#define PE_SIZE    0x1C    /* u16 packed size (without the trailing BCh) */
#define PE_PLANE   0x1E    /* u8 parked plane, FFh = none */
#define PE_PACKED  0x20    /* far packed data, NULL = not loaded */
#define PE_DATA    0x24
#define PE_MASK    0x28
#define PE_BPR     0x2C
#define PE_DRV     0x2E

#define PICDIR_SEG SEG(PICDIR_SEG_FILE)

/* Message pointers DS:4EE4.. (§5.7) */
#define MSG_INSERT_DISK DS_lib_msgs          /* "insert disk ? into drive ?" (+0Ch digit, +19h drive) */
#define MSG_COPY_ALL    (DS_lib_msgs + 2)    /* "copy all files to current directory" */
#define MSG_BAD_FORMAT  (DS_lib_msgs + 4)    /* "bad file format" */
#define MSG_WRITE_ERROR (DS_lib_msgs + 6)    /* "write error" */
#define MSG_NOT_FOUND   (DS_lib_msgs + 8)    /* "file not found: ????" (name at +10h) */

/* 0f38:23ce with a DGROUP string */
static void message_box(u16 s)
{
    modules.fatal_message(ds_str(s));
}

/* 0f38:6057 pic_index — platform.md §4.10: id >= 1000 -> id - 3DFh, else id - 1; CGA/Hercules order
 * through DS:07C6[]. */
s16 pic_index(s16 id)
{
    if (id >= 0x3E8) id = (s16)(id - 0x3DF);
    else id--;
    if (DSS(DS_driver_id) != -2 && DSS(DS_driver_id) != -6) return DSS((u16)(0x07C6 + 2 * id));
    return id;
}

/* Entry of the picture at directory index i (imul 30h, low word). */
static FarPtr pic_entry(u16 seg, s16 i)
{
    return far_make(seg, (u16)((s32)i * 0x30));
}

/* 0f38:6089 lib_select — platform.md §4.10: makes the library holding id the open one (closing the
 * other), prompts for its disk until it opens (3 tries), checks the picture counts. */
void lib_select(s16 id)
{
    s16 lib;
    if (pic_index(id) >= DSS(DS_lib1_count)) {
        lib = 2;
        DSW(DS_cur_lib_name) = DSW(DS_lib2_name);
        DSW(DS_cur_lib_handle) = DS_lib_handle + 2;
        DSW(DS_other_lib_handle) = DS_lib_handle;
        DSW(DS_cur_lib_count) = DS_lib2_count;
    } else {
        lib = 1;
        DSW(DS_cur_lib_name) = DSW(DS_lib1_name);
        DSW(DS_cur_lib_handle) = DS_lib_handle;
        DSW(DS_other_lib_handle) = DS_lib_handle + 2;
        DSW(DS_cur_lib_count) = DS_lib1_count;
    }
    if (DSW(DSW(DS_cur_lib_handle)) != 0) return;

    dos_close(DSW(DSW(DS_other_lib_handle)));                    /* (handle 0 if none: closes stdin) */
    DSW(DSW(DS_other_lib_handle)) = 0;
    u8 al;
    if (lib == 1) al = '1';
    else if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) al = '2';
    else al = '3';
    u16 m = DSW(MSG_INSERT_DISK);
    DSB(m + 0x0C) = al;
    DSB(m + 0x19) = DSB(DSW(DS_cur_lib_name));
    s16 si = 0;
    while (dos_open(ds_str(DSW(DS_cur_lib_name)), 0, &DSW(DSW(DS_cur_lib_handle))) != 0) {
        if (si++ == 3) platform_exit();
        message_box(DSW(MSG_INSERT_DISK));
    }
    u16 got = 0;
    if (dos_read(DSW(DSW(DS_cur_lib_handle)), ds_ptr(DSW(DS_cur_lib_count)), 2, &got) != 0 || got != 2 ||
        (u16)(DSW(DS_lib1_count) + DSW(DS_lib2_count)) != 0x115) {
        message_box(DSW(MSG_BAD_FORMAT));
        platform_exit();
    }
}

/* 0f38:61ab lib_read_dir — platform.md §4.10: the 30-byte records of LIB1 (lib 1) or LIB2 into the
 * directory, loader fields reset; returns the largest packed size (unsigned compare). */
s16 lib_read_dir(s16 lib)
{
    s16 base = (lib == 1) ? 0 : DSS(DS_lib1_count);
    lib_select(lib == 1 ? 1 : 0x3E8);
    u16 maxsz = 0;
    s16 count = (lib == 1) ? DSS(DS_lib1_count) : DSS(DS_lib2_count);
    if (count > 0) {
        u16 di = 2;                                              /* file position of the record */
        for (s16 si = 0; si < count; si++, di = (u16)(di + 0x1E)) {
            FarPtr e = pic_entry(SEG(0x389B), (s16)(base + si));
            u16 got = 0;
            if (lib_seek(DSW(DSW(DS_cur_lib_handle)), (s32)(s16)di) != 0 ||      /* cwd */
                dos_read(DSW(DSW(DS_cur_lib_handle)), e, 0x1E, &got) != 0 || got != 0x1E) {
                message_box(DSW(MSG_BAD_FORMAT));
                platform_exit();
            }
            wr8(e.seg, (u16)(e.off + PE_PLANE), 0xFF);
            far_wr(e.seg, (u16)(e.off + PE_PACKED), far_make(0, 0));
            far_wr(e.seg, (u16)(e.off + PE_DATA), far_make(0, 0));
            far_wr(e.seg, (u16)(e.off + PE_MASK), far_make(0, 0));
            wr16(e.seg, (u16)(e.off + PE_BPR), 0xFFFF);
            wr8(e.seg, (u16)(e.off + PE_DRV), 1);
            if (rd16(e.seg, (u16)(e.off + PE_SIZE)) > maxsz) maxsz = rd16(e.seg, (u16)(e.off + PE_SIZE));
        }
    }
    return (s16)maxsz;
}

/* 0f38:62ba pic_load — platform.md §4.10: 0 no memory, 1 already loaded, 2 loaded into RAM
 * (dst or far_alloc), 3 parked in one plane of the off-screen EGA memory (read into a temporary top
 * arena block, copied with movedata under map mask = that plane). size + 1 bytes are read; both the
 * first and the last must be BCh (floppy: retry after a disk prompt; hard disk: "bad file format"). */
s16 pic_load(s16 id, s16 to_vram, FarPtr dst)
{
    s16 idx = pic_index(id);
    FarPtr e = pic_entry(SEG(0x389B), idx);
    FarPtr vram = far_make(0, 0);
    u8 plane = 0;

    if (!far_is_null(far_rd(e.seg, (u16)(e.off + PE_PACKED)))) return 1;
    if (to_vram != 0) {
        vram = vram_pool_alloc(rd16(e.seg, (u16)(e.off + PE_SIZE)), &plane);
        FarPtr p = far_make(0, 0);
        if (!far_is_null(vram))
            p = arena_alloc((s16)(rd16(e.seg, (u16)(e.off + PE_SIZE)) + 1), 2);
        far_wr(e.seg, (u16)(e.off + PE_PACKED), p);
        wr8(e.seg, (u16)(e.off + PE_PLANE), plane);
    } else {
        FarPtr p = !far_is_null(dst) ? dst : far_alloc((u16)(rd16(e.seg, (u16)(e.off + PE_SIZE)) + 1), 0);
        far_wr(e.seg, (u16)(e.off + PE_PACKED), p);
        wr8(e.seg, (u16)(e.off + PE_PLANE), 0xFF);
    }
    if (far_is_null(far_rd(e.seg, (u16)(e.off + PE_PACKED)))) return 0;

    lib_select(id);
    /* (count * 1Eh + 2) sign-extended (imul low word, cwd) + the 32-bit offset */
    s32 pos = (s32)(s16)(u16)((s32)DSS(DSW(DS_cur_lib_count)) * 0x1E + 2);
    pos = (s32)((u32)pos + rd32(e.seg, (u16)(e.off + PE_OFFSET)));
    if (lib_seek(DSW(DSW(DS_cur_lib_handle)), pos) != 0) goto bad;
    if (rd8(e.seg, (u16)(e.off + PE_PLANE)) != 0xFF) {
        out(0x3C4, 2);
        out(0x3C5, (u8)(1u << (rd8(e.seg, (u16)(e.off + PE_PLANE)) & 0x1F)));   /* map mask: one plane */
    }
    s16 tries = 0;                                               /* [bp-16h] / si */
    for (;;) {
        u16 n = (u16)(rd16(e.seg, (u16)(e.off + PE_SIZE)) + 1), got = 0;   /* di */
        FarPtr b = far_rd(e.seg, (u16)(e.off + PE_PACKED));
        if (dos_read(DSW(DSW(DS_cur_lib_handle)), b, n, &got) != 0) goto bad;
        if (got != n) goto bad;
        if (rd8(b.seg, b.off) == 0xBC && rd8(b.seg, (u16)(b.off + n - 1)) == 0xBC) break;
        if (DSS(DS_cur_drive) > 2) goto bad;                     /* hard disk: no retry */
        do {                                                     /* floppy: ask for the disk again */
            if (++tries > 3) platform_exit();
            u8 al;
            if (DSW(DSW(DS_cur_lib_handle)) == DSW(DS_lib_handle)) al = 1;   /* sic: 01h, not '1' */
            else if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) al = '2';
            else al = '3';
            u16 m = DSW(MSG_INSERT_DISK);
            DSB(m + 0x0C) = al;
            DSB(m + 0x19) = DSB(DSW(DS_cur_lib_name));
            message_box(m);
            dos_close(DSW(DSW(DS_cur_lib_handle)));
        } while (dos_open(ds_str(DSW(DS_cur_lib_name)), 0, &DSW(DSW(DS_cur_lib_handle))) != 0);
        if (lib_seek(DSW(DSW(DS_cur_lib_handle)), pos) != 0) goto bad;
    }
    if (rd8(e.seg, (u16)(e.off + PE_PLANE)) == 0xFF) return 2;
    {
        FarPtr b = far_rd(e.seg, (u16)(e.off + PE_PACKED));
        vmovedata(b.seg, b.off, vram.seg, vram.off, rd16(e.seg, (u16)(e.off + PE_SIZE)));   /* to one plane */
        far_wr(e.seg, (u16)(e.off + PE_PACKED), vram);
        arena_pop(1);
        out(0x3C4, 2);
        out(0x3C5, 0x0F);
    }
    return 3;
bad:
    message_box(DSW(MSG_BAD_FORMAT));
    platform_exit();
}

/* 0f38:655e pic_free — platform.md §4.10: far_free the packed copy, entry +20h = NULL. */
void pic_free(s16 id)
{
    s16 idx = pic_index(id);
    FarPtr e = pic_entry(DSW(DS_picdir_seg), idx);
    FarPtr p = far_rd(e.seg, (u16)(e.off + PE_PACKED));
    if (!far_is_null(p)) {
        far_free(p);
        far_wr(e.seg, (u16)(e.off + PE_PACKED), far_make(0, 0));
    }
}

/* 0f38:65ae pic_decode — platform.md §4.10: loads the picture (into DS:7564 unless resident) and unpacks
 * it into dst (DS:4F0E: bitmap_alloc re-initialises dst first). A parked picture is read back through
 * the read map select GC4 = its plane (left set, sic); the map mask is restored to 0Fh. */
FarPtr pic_decode(s16 id, FarPtr dst)
{
    cursor_ctl(-4);                                              /* hide */
    s16 r = pic_load(id, 0, ds_far(DS_pack_buf));
    FarPtr e = pic_entry(SEG(0x389B), pic_index(id));
    if (DSW(DS_decode_alloc) != 0)
        dst = bitmap_alloc((s16)rd16(e.seg, (u16)(e.off + PE_W)), (s16)rd16(e.seg, (u16)(e.off + PE_H)),
                           (s16)rd8(e.seg, (u16)(e.off + PE_MASKED)), 1, dst);
    u8 tokens[16];                                               /* the stack copy [bp-16h] */
    for (u16 i = 0; i < 16; i++) tokens[i] = rd8(e.seg, (u16)(e.off + PE_TOKENS + i));
    if (rd8(e.seg, (u16)(e.off + PE_PLANE)) != 0xFF) {
        out(0x3CE, 4);
        out(0x3CF, rd8(e.seg, (u16)(e.off + PE_PLANE)));         /* read map select: the parked plane */
    }
    FarPtr src = far_rd(e.seg, (u16)(e.off + PE_PACKED));
    src.off++;                                                   /* skip the leading BCh */
    u16 n = (u16)(rd16(e.seg, (u16)(e.off + PE_SIZE)) - 1);
    FarPtr data = desc_planes(dst), mask = desc_mask(dst);
    if (DSS(DS_driver_id) == -2) {
        if (far_is_null(data)) {
            pic_unpack(src, mask, n, tokens, 0x10);
        } else {
            pic_unpack(src, data, n, tokens, 0x10);
            if (!far_is_null(desc_mask(dst))) make_mask_plane(desc_planes(dst), desc_mask(dst), desc_size(dst));
        }
    } else if (DSS(DS_driver_id) == -6) {
        /* PORT: Tandy (2462:0120 / 005a / 0229) not ported. */
    } else {
        pic_unpack(src, !far_is_null(data) ? data : mask, n, tokens, 0x10);
    }
    if (rd8(e.seg, (u16)(e.off + PE_PLANE)) != 0xFF) {
        out(0x3C4, 2);
        out(0x3C5, 0x0F);
    }
    if (r == 2) far_wr(e.seg, (u16)(e.off + PE_PACKED), far_make(0, 0));   /* the shared buffer */
    cursor_ctl(-2);                                              /* show */
    return dst;
}

/* 0f38:67ff pic_info — platform.md §4.10 */
void pic_info(s16 id, s16 *w, s16 *h, s16 *masked)
{
    FarPtr e = pic_entry(DSW(DS_picdir_seg), pic_index(id));
    *w = (s16)rd16(e.seg, (u16)(e.off + PE_W));
    *h = (s16)rd16(e.seg, (u16)(e.off + PE_H));
    *masked = (s16)rd8(e.seg, (u16)(e.off + PE_MASKED));
}

/* 0f38:683e pic_get — platform.md §4.10: an arena bitmap (0f38:9fce) with the picture unpacked. */
FarPtr pic_get(s16 id, s16 arena_mode)
{
    s16 w, h, m;
    pic_info(id, &w, &h, &m);
    FarPtr b = arena_bitmap_alloc(w, h, m, arena_mode);
    pic_decode(id, b);
    return b;
}

/* 0f38:6887 pic_park_list — platform.md §4.10: parks a 0-terminated id list in EGA memory; returns 1. */
s16 pic_park_list(u16 ids)
{
    if (DSW(DS_no_ega_park) != 0) return 1;
    for (u16 di = ids; DSW(di) != 0; di = (u16)(di + 2))
        pic_load((s16)DSW(di), 1, far_make(0, 0));
    return 1;
}

/* 0f38:68ca pic_load_list — platform.md §4.10: loads into RAM; counts successes, stops at the first
 * failure unless keep_going. */
s16 pic_load_list(u16 ids, s16 keep_going)
{
    s16 si = 0;
    for (;;) {
        u16 bx = ids;
        ids = (u16)(ids + 2);
        s16 id = (s16)DSW(bx);
        if (id == 0) return si;
        if (pic_load(id, 0, far_make(0, 0)) != 0) si++;
        else if (keep_going == 0) return si;
    }
}

/* 0f38:6915 pic_free_list — platform.md §4.10 */
void pic_free_list(u16 ids)
{
    for (u16 di = ids; DSW(di) != 0; di = (u16)(di + 2))
        pic_free((s16)DSW(di));
}

/* 1e16:22ec _bios_equiplist (INT 11h). PORT: one floppy drive (bit 0, bits 6-7 = 0); only reached on
 * a floppy drive, which the port never reports. */
static u16 bios_equiplist(void)
{
    return 0x0001;
}

/* 0f38:6943 lib_open_all — platform.md §4.10: the current drive, one or two floppy drives, LIB1 and LIB2
 * ("?:lib1"/"?:lib2", "?:lib1c"/"?:lib2c" for modes other than 2 and 5), their picture counts. */
void lib_open_all(s16 disks, s16 mode)
{
    dos_getdrive(&DSW(DS_cur_drive));
    if (DSS(DS_cur_drive) < 3 && disks > 1) {
        u16 eq = bios_equiplist();
        disks = ((eq & 1) && (eq & 0xC0)) ? 2 : 1;
    } else {
        disks = 1;
    }
    u16 n1 = (mode == 2 || mode == 5) ? DSW(DS_lib_names) : DSW(DS_lib_names + 2);
    DSW(DS_lib1_name) = n1;
    DSB(n1) = (u8)(DSB(DS_cur_drive) + 0x40);
    u16 err = dos_open(ds_str(n1), 0, &DSW(DS_lib_handle));
    if (err != 0 || DSW(DS_lib_handle) == 0) {
        if (DSS(DS_cur_drive) > 2) {
            message_box(DSW(MSG_COPY_ALL));
        } else {
            u16 bx = DSW(MSG_NOT_FOUND);
            strcpy(ds_str((u16)(bx + 0x10)), ds_str((u16)(DSW(DS_lib1_name) + 2)));   /* "file not found: lib1" */
            message_box(bx);
        }
        platform_exit();
    }
    u16 got = 0;
    if (dos_read(DSW(DS_lib_handle), ds_ptr(DS_lib1_count), 2, &got) != 0 || got != 2) {
        message_box(DSW(MSG_BAD_FORMAT));
        platform_exit();
    }
    u16 n2 = (mode == 2 || mode == 5) ? DSW(DS_lib_names + 4) : DSW(DS_lib_names + 6);
    DSW(DS_lib2_name) = n2;
    DSB(n2) = DSB(DSW(DS_lib1_name));
    if (dos_open(ds_str(n2), 0, &DSW(DS_lib_handle + 2)) != 0 || DSW(DS_lib_handle + 2) == 0) {
        if (DSS(DS_cur_drive) > 2) {
            message_box(DSW(MSG_COPY_ALL));
            platform_exit();
        } else if (disks == 1) {
            DSW(DS_lib_handle + 2) = 0;                          /* single drive: asked for later */
        } else {
            DSB(DSW(DS_lib2_name)) = (DSW(DS_cur_drive) == 1) ? 'B' : 'A';   /* the other floppy drive */
            err = dos_open(ds_str(DSW(DS_lib2_name)), 0, &DSW(DS_lib_handle + 2));
            s16 si = 0;
            s16 di = mode;
            while (err != 0 || DSW(DS_lib_handle + 2) == 0) {
                if (si == 3) platform_exit();
                u16 m = DSW(MSG_INSERT_DISK);
                DSB(m + 0x0C) = (di == 2 || di == 5) ? '2' : '3';
                DSB(m + 0x19) = DSB(DSW(DS_lib2_name));
                message_box(m);
                err = dos_open(ds_str(DSW(DS_lib2_name)), 0, &DSW(DS_lib_handle + 2));
                si++;
            }
        }
    }
    if (DSW(DS_lib_handle + 2) != 0) {
        if (dos_read(DSW(DS_lib_handle + 2), ds_ptr(DS_lib2_count), 2, &got) != 0 || got != 2 ||
            (u16)(DSW(DS_lib1_count) + DSW(DS_lib2_count)) != 0x115) {
            message_box(DSW(MSG_BAD_FORMAT));
            platform_exit();
        }
    }
}

/* ========================================================================== HOT_DATA, hall_dat */

/* 0f38:6b79 hot_data_load — platform.md §4.10, §5.4: "X:" of LIB2 + the name at DS:4EA2 into DS:6C72
 * (the copy loop advances the pointer DS:4EA2 itself, sic), 13 blocks straight into DGROUP, then
 * hall_load. */
void hot_data_load(void)
{
    u16 di = DS_hot_data_path;
    u16 bx = DSW(DS_lib2_name);
    DSB(di) = DSB(bx); di++;
    DSB(di) = DSB(bx + 1); di++;
    u16 si = DSW(DS_hot_data_name);
    u8 al;
    do {
        al = DSB(si); si++;
        DSB(di) = al; di++;
    } while (al != 0);
    DSW(DS_hot_data_name) = si;

    u16 h = 0;
    if (dos_open(ds_str(DS_hot_data_path), 0, &h) != 0) {
        message_box(0x4F10);                                     /* "Can't open data file" */
        platform_exit();
    }
    for (s16 i = 0; i < 0x0D; i++) {
        u16 dst = DSW((u16)(DS_hot_block_dst + 2 * i));
        u16 size = DSW((u16)(DS_hot_block_size + 2 * i));
        u16 got = 0;
        if (dos_read(h, ds_ptr(dst), size, &got) != 0 || size != got) {
            message_box(0x4F25);                                 /* "Data disc read fail" */
            platform_exit();
        }
    }
    dos_close(h);
    hall_load();
}

/* 0f38:6c3d data_disk_check — platform.md §4.10: returns the data drive letter; on a floppy waits
 * until DS:6C72 (hot_data) opens and re-selects LIB2. */
s16 data_disk_check(void)
{
    if (DSS(DS_cur_drive) > 2) return (s16)(s8)(u8)(DSB(DS_cur_drive) + 0x40);
    s16 si = 0;
    u16 h = 0;
    while (dos_open(ds_str(DS_hot_data_path), 0, &h) != 0) {
        si++;
        if (si > 3) platform_exit();
        u16 m = DSW(MSG_INSERT_DISK);
        DSB(m + 0x0C) = (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) ? '2' : '3';
        DSB(m + 0x19) = DSB(DSW(DS_lib2_name));
        message_box(m);
    }
    dos_close(h);
    lib_select(0x3E8);
    return (s16)DSC(DS_hot_data_path);
}

/* 0f38:6ccf hall_scramble — platform.md §5.6 */
void hall_scramble(void)
{
    for (s16 si = 0; si < 0xE6; si++) DSB(DS_hall_records + si) |= 0x80;
}

/* 0f38:6cec hall_unscramble — platform.md §5.6 */
void hall_unscramble(void)
{
    for (s16 si = 0; si < 0xE6; si++) DSB(DS_hall_records + si) &= 0x7F;
}

/* 0f38:6d09 hall_load — platform.md §4.10, §5.6: "X:hall_dat" on the LIB2 drive; count + count x 23
 * bytes (no upper bound, sic), or count 0 if it does not open. */
void hall_load(void)
{
    DSB(DS_hall_name) = DSB(DSW(DS_lib2_name));
    data_disk_check();
    u16 h = 0, got = 0;
    if (dos_open(ds_str(DS_hall_name), 0, &h) != 0 || h == 0) {
        DSW(DS_hall_count) = 0;
        return;
    }
    if (dos_read(h, ds_ptr(DS_hall_count), 2, &got) != 0 || got != 2) {
        message_box(DSW(MSG_BAD_FORMAT));
        platform_exit();
    }
    u16 si = DS_hall_records;
    for (s16 di = 0; di < DSS(DS_hall_count); di++, si = (u16)(si + 0x17)) {
        if (dos_read(h, ds_ptr(si), 0x17, &got) != 0 || got != 0x17) {
            message_box(DSW(MSG_BAD_FORMAT));
            platform_exit();
        }
    }
    dos_close(h);
    hall_unscramble();
}

/* 0f38:6dd2 hall_save — platform.md §4.10, §5.6: written scrambled; on a write error the box is shown
 * twice around remove() and the game goes on. */
void hall_save(void)
{
    hall_scramble();
    data_disk_check();
    u16 h = 0;                                                   /* PORT: uninitialised stack word in the original */
    u16 err = dos_creat(ds_str(DS_hall_name), 0, &h), got = 0;
    if (DSS(DS_cur_drive) > 2) {
        if (h == 0 || err != 0) {
            message_box(DSW(MSG_WRITE_ERROR));
            platform_exit();
        }
    } else {
        u16 m = DSW(MSG_INSERT_DISK);
        DSB(m + 0x0C) = '1';                                     /* prepared but not shown (sic) */
        DSB(m + 0x19) = DSB(DS_hall_name);
        s16 si = 0;
        while (h == 0 || err != 0) {
            if (si == 3) platform_exit();
            message_box(DSW(MSG_BAD_FORMAT));                    /* sic */
            err = dos_creat(ds_str(DS_hall_name), 0, &h);
            si++;
        }
    }
    if (dos_write(h, ds_ptr(DS_hall_count), 2, &got) != 0 || got != 2) goto fail;
    {
        u16 p = DS_hall_records;
        for (s16 i = 0; i < DSS(DS_hall_count); i++, p = (u16)(p + 0x17))
            if (dos_write(h, ds_ptr(p), 0x17, &got) != 0 || got != 0x17) goto fail;
    }
    if (dos_close(h) == 0) goto done;
fail:
    message_box(DSW(MSG_WRITE_ERROR));
    dos_remove(ds_str(DS_hall_name));
    message_box(DSW(MSG_WRITE_ERROR));
done:
    hall_unscramble();
}

/* ============================================================================== random numbers */

/* 0f38:5eb6 rnd — platform.md §4.11: Wichmann-Hill, all 16-bit signed idiv / imul (low word). */
static s16 wh_step(s16 s, s16 q, s16 a, s16 r, s16 m)
{
    s16 rem;
    s16 quo = idiv32_16(s, q, &rem);
    s16 v = (s16)(u16)((u16)((s32)rem * a) - (u16)((s32)quo * r));
    if (v < 0) v = (s16)(v + m);
    return v;
}

s16 rnd(s16 n)
{
    if (n <= 0) {
        s16 t = (s16)(u16)(u32)time(NULL);                       /* low word of time(&t) */
        s16 rem;
        idiv32_16(t, 0x7530, &rem);
        s16 dx = (s16)(rem + 1);
        DSS(DS_rnd_s1) = dx;
        DSS(DS_rnd_s2) = dx;
        DSS(DS_rnd_s3) = dx;
        s16 si = dx;
        do {
            si = wh_step(si, 0xB1, 0xAB, 2, 0x763D);             /* sic: the s1 step for s2 */
        } while (si > 0x7530);
        DSS(DS_rnd_s2) = si;
        si = DSS(DS_rnd_s3);
        do {
            si = wh_step(si, 0xB2, 0xAA, 0x3F, 0x7673);
        } while (si > 0x7530);
        DSS(DS_rnd_s3) = si;
        return -1;
    }
    DSS(DS_rnd_s1) = wh_step(DSS(DS_rnd_s1), 0xB1, 0xAB, 2, 0x763D);
    DSS(DS_rnd_s2) = wh_step(DSS(DS_rnd_s2), 0xB0, 0xAC, 0x23, 0x7663);
    DSS(DS_rnd_s3) = wh_step(DSS(DS_rnd_s3), 0xB2, 0xAA, 0x3F, 0x7673);
    s16 cx, si, dx;
    idiv32_16(DSS(DS_rnd_s3), n, &cx);
    idiv32_16(DSS(DS_rnd_s2), n, &si);
    idiv32_16(DSS(DS_rnd_s1), n, &dx);
    s16 ax = (s16)(u16)((u16)dx + (u16)si + (u16)cx);
    idiv32_16(ax, n, &dx);
    return dx;
}
