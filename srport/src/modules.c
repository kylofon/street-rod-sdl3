/* Wiring of the cross-subsystem hooks (modules.h). Each stand-in below is replaced by the ported
 * function once its subsystem lands: e.g.  modules.demo_step = demo_step;  */
#include "modules.h"

#include <stdio.h>

#include "host.h"
#include "platform/platform.h"

Modules modules;

/* ---- stand-ins (PORT: temporary, until the owning subsystem is ported) */

static void nop(void) {}
static void nop_s16(s16 v) { (void)v; }
static s16  no_hotspot(s16 x, s16 y) { (void)x; (void)y; return 0; }

/* 0f38:23ce draws a message box and waits for a key; the stand-in reports the text and waits for a
 * key through the platform (as the original does in text mode, g_ui_level < 1). */
static void fatal_message_stub(const char *s)
{
    fprintf(stderr, "message: %s\n", s);
    while (key_read() == 0) host_pump();
}

void modules_init(void)
{
    modules.race_phys_step = nop;

    modules.demo_step = nop;

    modules.timer_callback = nop_s16;
    modules.hotspot_at = no_hotspot;
    modules.fatal_message = fatal_message_stub;
}
