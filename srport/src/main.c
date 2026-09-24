/* Street Rod SDL3 port — entry point.
 *
 * usage: srport [--game-dir DIR] [--scale N] [--fullscreen] [--check]
 *   --game-dir    folder with the original game files (default: "Game" in the working directory)
 *   --scale       initial window scale: 320x240 times N (default 3)
 *   --fullscreen  start in full screen (Alt+Enter switches)
 *   --check       load and verify the original executable, print a summary and exit (no window)
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "mem.h"
#include "platform/ega.h"
#include "game/game.h"

static int usage(const char *prog)
{
    fprintf(stderr, "usage: %s [--game-dir DIR] [--scale N] [--fullscreen] [--check]\n", prog);
    return 2;
}

int main(int argc, char **argv)
{
    const char *dir = "Game";
    int scale = 3;
    bool check = false, fullscreen = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--game-dir") && v) { dir = v; i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--check")) check = true;
        else return usage(argv[0]);
    }

    char exe_path[1024];
    snprintf(exe_path, sizeof exe_path, "%s/%s", dir, SR_EXE_NAME);
    char err[256];
    if (!mem_load_exe(exe_path, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Street Rod", err, NULL);
        return 1;
    }
    if (check) {
        printf("%s ok: image %u bytes at %04X:0000, DGROUP %04X\n", SR_EXE_NAME, mem_image_size, LOAD_SEG, DGROUP);
        return 0;
    }

    if (!host_init(dir, scale, fullscreen)) return 1;
    ega_init();      /* 16-colour planar model: frame source, planes, CRTC start / line compare */
    int rc = game_main();
    host_shutdown();
    return rc;
}
