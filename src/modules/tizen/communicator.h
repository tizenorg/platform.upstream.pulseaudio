#ifndef foocommunicatorfoo
#define foocommunicatorfoo
#include <pulsecore/core.h>

typedef enum pa_communicator_hook {
    PA_COMMUNICATOR_HOOK_SELECT_INIT_SINK_OR_SOURCE,
    PA_COMMUNICATOR_HOOK_CHANGE_ROUTE,
    PA_COMMUNICATOR_HOOK_UPDATE_ROUTE_OPTION,
    PA_COMMUNICATOR_HOOK_DEVICE_CONNECTION_CHANGED,
    PA_COMMUNICATOR_HOOK_DEVICE_INFORMATION_CHANGED,
    PA_COMMUNICATOR_HOOK_MAX
} pa_communicator_hook_t;

typedef struct _pa_communicator pa_communicator;

pa_communicator* pa_communicator_get(pa_core *c);
pa_communicator* pa_communicator_ref(pa_communicator *c);
void pa_communicator_unref(pa_communicator *c);
pa_hook* pa_communicator_hook(pa_communicator *c, pa_communicator_hook_t hook);

#endif
