/* Skeleton stand-in for the game (removed when game_flow is ported): shows the title picture
 * (LIB1 #0) through the real unpacker and the EGA model, Esc quits. It proves the pipeline: EXE
 * loader, host, tick, keyboard, planar scan-out, library format. */
#include <SDL3/SDL.h>
#include <string.h>

#include "game/game.h"
#include "host.h"
#include "mem.h"
#include "platform/ega.h"
#include "platform/pic.h"

static volatile bool quit;

static void on_key(u8 byte)
{
    if (byte == 0x01) quit = true;            /* Esc make code */
}

static void show_lib_picture(const char *lib, int index)
{
    char *path = host_game_path(lib, false);
    if (!path) host_fatal("%s not found in the game folder", lib);
    size_t len;
    u8 *f = SDL_LoadFile(path, &len);
    host_free(path);
    if (!f) host_fatal("cannot read %s", lib);

    u16 count = (u16)(f[0] | f[1] << 8);
    if (index >= count) host_fatal("%s has no picture %d", lib, index);
    const u8 *rec = f + 2 + index * 30;
    u16 w = (u16)(rec[0] | rec[1] << 8), h = (u16)(rec[2] | rec[3] << 8);
    u16 raw = (u16)(rec[4] | rec[5] << 8);
    u32 off = (u32)(rec[24] | rec[25] << 8 | rec[26] << 16 | (u32)rec[27] << 24);
    u16 size = (u16)(rec[28] | rec[29] << 8);
    const u8 *data = f + 2 + count * 30 + off;
    if (data[0] != 0xBC) host_fatal("%s picture %d: bad marker", lib, index);

    u8 *pix = SDL_calloc(1, (size_t)raw + 0x100);
    pic_unpack(data + 1, pix, (u16)(size - 1), rec + 7, 16);
    u16 bpr = (u16)(w / 8), plane = (u16)(bpr * h);
    for (int p = 0; p < 4; p++)
        for (int y = 0; y < h && y < 200; y++)
            memcpy(ega_plane(p) + y * EGA_BYTES_PER_LINE, pix + p * plane + y * bpr, bpr);
    ega_touch();
    SDL_free(pix);
    SDL_free(f);
}

int game_main(void)
{
    host_set_kbd_handler(on_key);
    show_lib_picture("LIB1", 0);
    while (!quit) host_pump();
    return 0;
}
