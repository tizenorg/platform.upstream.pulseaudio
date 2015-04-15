#ifndef foohalmanagerfoo
#define foohalmanagerfoo
#include <dlfcn.h>
#include <pulsecore/core.h>
#include "tizen-audio.h"
/* TODO : move below structure to hal-manager.c */
struct _pa_hal_manager {
    PA_REFCNT_DECLARE;

    pa_core *core;
    void *dl_handle;
    void *data;
    audio_interface_t intf;
};

typedef struct _pa_hal_manager pa_hal_manager;

pa_hal_manager* pa_hal_manager_get(pa_core *core, void *user_data);
pa_hal_manager* pa_hal_manager_ref(pa_hal_manager *h);
void pa_hal_manager_unref(pa_hal_manager *h);
audio_return_t pa_hal_get_buffer_attribute(pa_hal_manager *h, audio_latency_t latency, uint32_t samplerate, audio_sample_format_t format, uint32_t channels, uint32_t *maxlength, uint32_t *tlength, uint32_t *prebuf, uint32_t* minreq, uint32_t *fragsize);

#endif
