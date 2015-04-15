/***
  This file is part of PulseAudio.

  Copyright 2015 Sangchul Lee <sc11.lee@samsung.com>

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "hal-manager.h"
#if 0
struct _pa_hal_manager {
    PA_REFCNT_DECLARE;

    pa_core *core;
    void *dl_handle;
    void *data;
    audio_interface_t intf;
};
#endif

/* Audio HAL library */
#define LIB_TIZEN_AUDIO "libtizen-audio.so"

pa_hal_manager* pa_hal_manager_get(pa_core *core, void *user_data) {
    pa_hal_manager *h;
    unsigned i;

    pa_assert(core);

    if ((h = pa_shared_get(core, "hal-manager")))
        return pa_hal_manager_ref(h);

    h = pa_xnew0(pa_hal_manager, 1);
    PA_REFCNT_INIT(h);
    h->core = core;

    /* Load library & init HAL manager */
    h->dl_handle = dlopen(LIB_TIZEN_AUDIO, RTLD_NOW);
    if (h->dl_handle) {
        h->intf.init = dlsym(h->dl_handle, "audio_init");
        h->intf.deinit = dlsym(h->dl_handle, "audio_deinit");
        //h->intf.reset = dlsym(h->dl_handle, "audio_reset");
        //h->intf.set_callback = dlsym(h->dl_handle, "audio_set_callback");
        h->intf.get_volume_level_max = dlsym(h->dl_handle, "audio_get_volume_level_max");
        h->intf.get_volume_level = dlsym(h->dl_handle, "audio_get_volume_level");
        h->intf.set_volume_level = dlsym(h->dl_handle, "audio_set_volume_level");
        h->intf.get_volume_value = dlsym(h->dl_handle, "audio_get_volume_value");
        h->intf.get_mute = dlsym(h->dl_handle, "audio_get_mute");
        h->intf.set_mute = dlsym(h->dl_handle, "audio_set_mute");
        h->intf.alsa_pcm_open = dlsym(h->dl_handle, "audio_alsa_pcm_open");
        h->intf.alsa_pcm_close = dlsym(h->dl_handle, "audio_alsa_pcm_close");
        h->intf.pcm_open = dlsym(h->dl_handle, "audio_pcm_open");
        h->intf.pcm_close = dlsym(h->dl_handle, "audio_pcm_close");
        h->intf.pcm_avail = dlsym(h->dl_handle, "audio_pcm_avail");
        h->intf.pcm_write = dlsym(h->dl_handle, "audio_pcm_write");
        h->intf.do_route = dlsym(h->dl_handle, "audio_do_route");
        h->intf.update_route_option = dlsym(h->dl_handle, "audio_update_route_option");
        h->intf.get_buffer_attr = dlsym(h->dl_handle, "audio_get_buffer_attr");
        if (h->intf.init) {
            /* TODO : no need to pass platform_data as second param. need to fix hal. */
            if (h->intf.init(&h->data, user_data) != AUDIO_RET_OK) {
                pa_log_error("hal_manager init failed");
            }
        }
#if 1 /* remove comment after enable NEW_HAL */
        pa_shared_set(core, "tizen-audio-data", h->data);
        pa_shared_set(core, "tizen-audio-interface", &h->intf);
#endif

     } else {
         pa_log_error("open hal_manager failed :%s", dlerror());
         return NULL;
     }

    pa_shared_set(core, "hal-manager", h);

    return h;
}

pa_hal_manager* pa_hal_manager_ref(pa_hal_manager *h) {
    pa_assert(h);
    pa_assert(PA_REFCNT_VALUE(h) > 0);

    PA_REFCNT_INC(h);

    return h;
}

void pa_hal_manager_unref(pa_hal_manager *h) {
    pa_assert(h);
    pa_assert(PA_REFCNT_VALUE(h) > 0);

    if (PA_REFCNT_DEC(h) > 0)
        return;

    /* Deinit HAL manager & unload library */
    if (h->intf.deinit) {
        if (h->intf.deinit(&h->data) != AUDIO_RET_OK) {
            pa_log_error("hal_manager deinit failed");
        }
    }
    if (h->dl_handle) {
        dlclose(h->dl_handle);
    }

    if (h->core)
        pa_shared_remove(h->core, "hal-manager");

    pa_xfree(h);
}

audio_return_t pa_hal_get_buffer_attribute(pa_hal_manager *h, audio_latency_t latency, uint32_t samplerate, audio_sample_format_t format, uint32_t channels, uint32_t *maxlength, uint32_t *tlength, uint32_t *prebuf, uint32_t* minreq, uint32_t *fragsize) {
    audio_return_t ret = AUDIO_RET_OK;

    pa_assert(h);

    pa_log_info("latency:%d, rate:%u, format:%d, channels:%u)", latency, samplerate, format, channels);
    if (h->intf.get_buffer_attr) {
        ret = h->intf.get_buffer_attr(h->data, latency, samplerate, format, channels, maxlength, tlength, prebuf, minreq, fragsize);
        if (ret != AUDIO_RET_OK) {
            pa_log_error("Failed get_buffer_attr() - ret:%d", ret);
        } else {
            pa_log_info("maxlength:%d, tlength:%d, prebuf:%d, minreq:%d, fragsize:%d", maxlength, tlength, prebuf, minreq, fragsize);
        }
    }

    return ret;
}
