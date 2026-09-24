/* Hook registration of the flow subsystem (modules.h). Owned by the flow port. */
#include "game/flow.h"
#include "modules.h"

void flow_register_hooks(void)
{
    modules.demo_step = demo_step;                /* 0000:1bae, from timer_isr in demo mode */
}
