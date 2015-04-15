#ifndef foostreammanagerfoo
#define foostreammanagerfoo
#include <pulsecore/core.h>

#include "stream-manager-priv.h"

typedef struct _stream_manager pa_stream_manager;

typedef enum _stream_type {
    STREAM_SINK_INPUT,
    STREAM_SOURCE_OUTPUT,
} stream_type;

typedef struct _device {
    char *type;
    int id;
} device;

typedef struct _hook_call_data_for_select {
    char *stream_role;
    stream_type stream_type;
    stream_route_type route_type;
    pa_sink **proper_sink;
    pa_source **proper_source;
    pa_sample_spec sample_spec;
    pa_idxset *avail_devices;
    pa_hashmap *manual_devices;
} pa_stream_manager_hook_data_for_select;

typedef struct _hook_call_data_for_route {
    char *stream_role;
    stream_type stream_type;
    stream_route_type route_type;
    pa_sample_spec sample_spec;
    pa_idxset *route_options;
    pa_idxset *avail_devices;
    pa_hashmap *manual_devices;
    pa_idxset *streams;
} pa_stream_manager_hook_data_for_route;

typedef struct _hook_call_data_for_options {
    char *stream_role;
    pa_idxset *route_options;
} pa_stream_manager_hook_data_for_options;

pa_stream_manager* pa_stream_manager_init(pa_core *c);
void pa_stream_manager_done(pa_stream_manager* m);

#endif
