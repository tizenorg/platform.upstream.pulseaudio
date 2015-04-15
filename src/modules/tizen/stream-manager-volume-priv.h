#ifndef foostreammanagervolumeprivfoo
#define foostreammanagervolumeprivfoo

#include "stream-manager.h"

int init_volume_map (pa_stream_manager *m);
void deinit_volume_map (pa_stream_manager *m);
int set_volume_level_by_idx(pa_stream_manager *m, stream_type stream_type, uint32_t idx, uint32_t volume_level);
#ifdef PRIMARY_VOLUME
int _set_primary_volume(pa_stream_manager *m, void* key, int volumetype, int is_new);
#endif

#endif
