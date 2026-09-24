/* Street Rod SDL3 port — entry point.
 *
 * usage: srport [--game-dir DIR] [--scale N] [--fullscreen] [--check] [switches of SR.EXE]
 *   --game-dir    folder with the original game files (default: "Game" in the working directory)
 *   --scale       initial window scale: 320x240 times N (default 3)
 *   --fullscreen  start in full screen (Alt+Enter switches)
 *   --check       load and verify the original executable, print a summary and exit (no window)
 *   the original's command-line switches are passed on as they are: 1, nomouse, demo, auto, nouemem
 *   (platform.md §5.1)
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game.h"
#include "host.h"
#include "mem.h"
#include "modules.h"
#include "platform/ega.h"
#include "platform/platform.h"

#define MAX_GAME_ARGS 16

static int usage(const char *prog)
{
    fprintf(stderr, "usage: %s [--game-dir DIR] [--scale N] [--fullscreen] [--check] "
                    "[1] [nomouse] [demo] [auto] [nouemem]\n", prog);
    return 2;
}

int main(int argc, char **argv)
{
    const char *dir = "Game";
    int scale = 3;
    bool check = false, fullscreen = false;
    char *game_argv[MAX_GAME_ARGS + 1] = { argv[0] };
    int game_argc = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--game-dir") && v) { dir = v; i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--check")) check = true;
        else if (a[0] == '-' && a[1] == '-') return usage(argv[0]);
        else if (game_argc < MAX_GAME_ARGS) game_argv[game_argc++] = argv[i];
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
    ega_init();                     /* 16-colour planar display: frame source, planes */
    modules_init();                 /* hooks into subsystems ported separately (modules.h) */
    platform_main_init(game_argc, game_argv);   /* main 0000:066f up to the game loop */
    int rc = game_main();           /* game_loop 0000:503f, platform_exit 0000:0fea */
    host_shutdown();
    return rc;
}
