#ifndef foostreammanagervolumeprivfoo
#define foostreammanagervolumeprivfoo

#include "stream-manager.h"

#include <vconf.h>
#include <vconf-keys.h>

#define VCONFKEY_OUT_VOLUME_PREFIX         "file/private/sound/volume/"

typedef enum {
    GET_VOLUME_CURRENT_LEVEL,
    GET_VOLUME_MAX_LEVEL
} pa_volume_get_command_t;

int32_t init_volume_map (pa_stream_manager *m);
void deinit_volume_map (pa_stream_manager *m);
int32_t get_volume_level_by_type(pa_stream_manager *m, pa_volume_get_command_t command, stream_type_t stream_type, const char *volume_type, uint32_t *volume_level);
int32_t set_volume_level_by_type(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t volume_level);
int32_t set_volume_level_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t idx, uint32_t volume_level);
int32_t get_volume_mute_by_type(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, pa_bool_t *mute);
int32_t set_volume_mute_by_type(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, pa_bool_t mute);
int32_t set_volume_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, pa_bool_t mute);
int32_t get_volume_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, pa_bool_t *mute);

#endif
