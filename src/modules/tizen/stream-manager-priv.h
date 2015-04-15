#ifndef foostreammanagerprivfoo
#define foostreammanagerprivfoo

#include "hal-manager.h"
#include "communicator.h"

#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>
#ifdef HAVE_DBUS
#include <pulsecore/dbus-shared.h>
#include <pulsecore/protocol-dbus.h>
#include <pulsecore/dbus-util.h>
#endif

enum stream_direction {
    STREAM_DIRECTION_IN,
    STREAM_DIRECTION_OUT,
    STREAM_DIRECTION_MAX,
};

typedef enum stream_route_type {
    STREAM_ROUTE_TYPE_AUTO,     /* the policy of decision device(s) is automatic and it's routing path is particular to one device */
    STREAM_ROUTE_TYPE_AUTO_ALL, /* the policy of decision device(s) is automatic and it's routing path can be several devices */
    STREAM_ROUTE_TYPE_MANUAL,   /* the policy of decision device(s) is manual */
} stream_route_type;


typedef struct _stream_info {
    int32_t priority;
    char *volume_type[STREAM_DIRECTION_MAX];
    pa_bool_t is_hal_volume[STREAM_DIRECTION_MAX];
    stream_route_type route_type;
    pa_idxset *idx_avail_in_devices;
    pa_idxset *idx_avail_out_devices;
    pa_idxset *idx_avail_frameworks;
} stream_info;

typedef struct _volume_info {
    pa_bool_t is_hal_volume_type;
    pa_idxset *idx_volume_values;
    uint32_t current_level;
} volume_info;

typedef struct _volume_map {
    pa_hashmap *in_volumes;
    pa_hashmap *out_volumes;
    pa_hashmap *in_modifier_gains;
    pa_hashmap *out_modifier_gains;
} volume_map;

typedef struct _prior_max_priority_stream {
    pa_sink_input *sink_input;
    pa_source_output *source_output;
} cur_max_priority_stream;

struct primary_volume_type_info {
    void* key;
    int volumetype;
    int priority;
    PA_LLIST_FIELDS(struct primary_volume_type_info);
};

struct _stream_manager {
    pa_core *core;
    pa_hal_manager *hal;
    pa_hashmap *stream_map;
    volume_map volume_map;
    pa_hashmap *stream_parents;
    cur_max_priority_stream cur_highest_priority;
    pa_hook_slot
        *sink_input_new_slot,
        *sink_input_put_slot,
        *sink_input_unlink_slot,
        *sink_input_state_changed_slot,
        *source_output_new_slot,
        *source_output_put_slot,
        *source_output_unlink_slot,
        *source_output_state_changed_slot;
#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    pa_dbus_protocol *dbus_protocol;
#else
    pa_dbus_connection *dbus_conn;
#endif
#endif
    pa_subscription *subscription;
    pa_communicator *comm;
#ifdef PRIMARY_VOLUME
    PA_LLIST_HEAD(struct primary_volume_type_info, primary_volume);
#endif
};


#endif
