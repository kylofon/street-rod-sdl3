/* Hook registration of the garage subsystem (modules.h). Owned by the garage port. */
#include "game/garage.h"
#include "game/ui.h"
#include "modules.h"

void garage_register_hooks(void)
{
    modules.timer_callback = anim_tick;           /* 0f38:1e41 */
    modules.hotspot_at = hotspot_at;              /* 0f38:4dad */
    modules.fatal_message = fatal_message;        /* 0f38:23ce */
}
