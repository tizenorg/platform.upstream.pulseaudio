#ifndef foostreammanagervolumefoo
#define foostreammanagervolumefoo

/* TODO : do not include tizen-audio.h for volume type, volume type will be string type. */
#include "tizen-audio.h"

#include "stream-manager.h"
#include <vconf.h>
#include <vconf-keys.h>

#define VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE "memory/private/sound/PrimaryVolumetype"

audio_return_t pa_stream_manager_volume_get_level_max(pa_stream_manager *m, stream_type stream_type, const char *volume_type, uint32_t *volume_level);
audio_return_t pa_stream_manager_volume_get_current_level(pa_stream_manager *m, stream_type stream_type, const char *volume_type, uint32_t *volume_level);
audio_return_t pa_stream_manager_volume_set_level(pa_stream_manager *m, stream_type stream_type, const char *volume_type, uint32_t volume_level);
audio_return_t pa_stream_manager_volume_get_mute(pa_stream_manager *m, stream_type stream_type, const char *volume_type, pa_bool_t *mute);
audio_return_t pa_stream_manager_volume_set_mute(pa_stream_manager *m, stream_type stream_type, const char *volume_type, pa_bool_t mute);
audio_return_t pa_stream_manager_volume_get_mute_by_idx(pa_stream_manager *m, stream_type stream_type, uint32_t stream_idx, pa_bool_t *mute);
audio_return_t pa_stream_manager_volume_set_mute_by_idx(pa_stream_manager *m, stream_type stream_type, uint32_t stream_idx, pa_bool_t mute);

#endif
