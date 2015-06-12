#ifndef foostreammanagerfoo
#define foostreammanagerfoo
#include <pulsecore/core.h>

typedef struct _stream_manager pa_stream_manager;

typedef enum _stream_type {
    STREAM_SINK_INPUT,
    STREAM_SOURCE_OUTPUT,
} stream_type;

typedef enum stream_route_type {
    STREAM_ROUTE_TYPE_AUTO,     /* the policy of decision device(s) is automatic and it's routing path is particular to one device */
    STREAM_ROUTE_TYPE_AUTO_ALL, /* the policy of decision device(s) is automatic and it's routing path can be several devices */
    STREAM_ROUTE_TYPE_MANUAL,   /* the policy of decision device(s) is manual */
} stream_route_type;

typedef struct _device {
    char *type;
    int direction;
    int id;
} device;

typedef struct _hook_call_data_for_select {
    stream_type stream_type;
    char *stream_role;
    stream_route_type route_type;
    device *device_list;
    int device_list_len;
    pa_sink **proper_sink;
    pa_source **proper_source;
    pa_sample_spec sample_spec;
} pa_stream_manager_hook_data_for_select;

typedef struct _hook_call_data_for_route {
    stream_type stream_type;
    char *stream_role;
    stream_route_type route_type;
    device *device_list;
    int device_list_len;
    pa_sample_spec sample_spec;
} pa_stream_manager_hook_data_for_route;

typedef struct _hook_call_data_for_option {
    char *stream_role;
    char **option_list;
    int option_list_len;
} pa_stream_manager_hook_data_for_option;

pa_stream_manager* pa_stream_manager_init(pa_core *c);
void pa_stream_manager_done(pa_stream_manager* m);

#endif
